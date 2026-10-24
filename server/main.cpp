#include <array>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#include <mutex>
#include <print>
#include <stdexcept>
#include <thread>
#include <vector>

#include <CLI/CLI.hpp>

#include "data/GCMD_1/FrameHeader.h"
#include "serialization/Decoder.h"
#include "serialization/Encoder.h"
#include "util/BytesParser.h"
#include "util/FixedPointParser.h"
#include "util/NetworkHelper.h"

static uint64_t getTimestamp(std::chrono::time_point<std::chrono::steady_clock> startTime) {
    auto elapsed{std::chrono::steady_clock::now() - startTime};
    return static_cast<uint64_t>(elapsed.count());
}

static bool serveMDClient(const int clientSockfd, const std::array<char, 12> &symbol, L1State &state,
                          std::stop_token st) {
    bool success{true};
    std::vector<std::byte> accumBuf;
    std::array<std::byte, 1024> receivedBuf{};
    while (!st.stop_requested()) {
        ssize_t bytesReceived = recv(clientSockfd, receivedBuf.data(), receivedBuf.size(), 0);
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                success = false;
            break;
        }

        try {
            processQuoteBytes(accumBuf, std::span(receivedBuf.begin(), bytesReceived), symbol, state);
        } catch (std::invalid_argument &e) {
            std::println("{}", e.what());
            success = false;
            break;
        }
    }
    close(clientSockfd);
    return success;
}

static void serveOEClient(const int clientSockfd, const std::array<char, 12> &symbol, const L1State &l1State,
                          RollingVwap<VWAP_CAPACITY> &vwap, SessionState &sessionState,
                          const DecisionConfig &config, std::chrono::time_point<std::chrono::steady_clock> startTime,
                          std::stop_token st, std::atomic<bool> &open) {
    constexpr auto timeoutMs{5000};
    std::vector<std::byte> accumBuf;
    accumBuf.reserve(4096);

    pollfd pfd{};
    pfd.fd = clientSockfd;
    pfd.events = POLLIN;

    bool isOpen{};

    using namespace std::chrono;
    std::vector<uint64_t> tickToOrderTimes;
    std::vector<NewOrder> acceptedOrders;

    std::array<std::byte, 1024> receivedBuf{};

    while (true) {
        time_point<steady_clock> decisionStart, decisionEnd;
        if (st.stop_requested()) {
            SessionControl sc{
                .ts_ns = getTimestamp(startTime),
                .state = CLOSE
            };
            std::array<std::byte, FrameHeader::SIZE + SessionControl::SIZE> bytes{};
            try {
                Encode(sc, bytes);
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;
            }
            if (!sendAll(clientSockfd, bytes))
                std::println("Failed to send close command to OE client");
            break;
        }

        if (const bool bl{open.load(std::memory_order_relaxed)}; bl != isOpen) {
            const uint8_t state = bl ? OPEN : HALT;
            SessionControl sc{
                .ts_ns = getTimestamp(startTime),
                .state = state
            };
            std::array<std::byte, FrameHeader::SIZE + SessionControl::SIZE> bytes{};
            try {
                Encode(sc, bytes);
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;
                break;
            }
            if (!sendAll(clientSockfd, bytes)) {
                std::string stateStr = state == OPEN ? "open" : "halt";
                std::println("Failed to send {} command to OE client", stateStr);
            }
            isOpen = bl;
        }

        int ready{poll(&pfd, 1, timeoutMs)};

        if (ready < 0) {
            if (errno == EINTR)
                continue;
            std::println("poll failed: {}", strerror(errno));
            break;
        }
        if (ready == 0) {
            // Timed out after 5 seconds; send a heartbeat
            Heartbeat hb{.ts_ns = getTimestamp(startTime)};
            std::array<std::byte, FrameHeader::SIZE + Heartbeat::SIZE> bytes{};
            Encode(hb, bytes);
            if (!sendAll(clientSockfd, bytes))
                std::println("Failed to send heartbeat to OE client");
            continue;
        }

        ssize_t bytesReceived = recv(clientSockfd, receivedBuf.data(), receivedBuf.size(), 0);
        decisionStart = steady_clock::now();
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                std::println("recv failed: {}", strerror(errno));
            break;
        }
        L1Snapshot snapshot{};
        {
            std::scoped_lock lock(l1State.stateMutex);
            snapshot.best_ask_px = l1State.ask_px;
            snapshot.best_bid_px = l1State.bid_px;
            snapshot.quotesObserved = l1State.quotesObserved;
        }

        auto onOrderReceived = [&](NewOrder& order) {
            if (isOpen) {
                std::array<std::byte, FrameHeader::SIZE + NewOrder::SIZE> bytes{};
                order.ts_ns += static_cast<uint64_t>((steady_clock::now() - decisionStart).count());
                Encode(order, bytes);
                decisionEnd = steady_clock::now();
                tickToOrderTimes.push_back(duration_cast<nanoseconds>(decisionEnd - decisionStart).count());
                if (!sendAll(clientSockfd, bytes))
                    std::println("Failed to send new order to OE client");
                else {
                    if (order.status == 'A')
                        acceptedOrders.push_back(order);
                    std::println("New order sent");
                }
            } else
                std::println("No new order sent (halted)");
        };

        const auto timeElapsed{steady_clock::now() - startTime};
        try {
            processTradeBytes(accumBuf, std::span(receivedBuf.begin(), bytesReceived), symbol, snapshot,
                sessionState, config, timeElapsed, vwap, onOrderReceived);
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
            break;
        }
    }
    close(clientSockfd);

    // Print execution report
    std::println();
    std::println("=== SLIPSTREAM EXECUTION REPORT ===");

    std::string symbolStr(symbol.begin(), std::ranges::find(symbol, '\0'));
    std::println("symbol\t\t\t\t{}", symbolStr);

    std::println("market qty\t\t\t{}", sessionState.market_volume);

    std::println("executed qty\t\t{} ({:.2f}%)", sessionState.executed_qty,
        static_cast<double>(sessionState.executed_qty) / static_cast<double>(sessionState.market_volume) * 100.0);

    long double avgFill{
        sessionState.executed_qty == 0 ? 0
        : static_cast<double>(sessionState.executed_notional) / static_cast<double>(sessionState.executed_qty)
    };
    std::println("avg fill price\t\t{:.2f}", avgFill);

    int64_t sessionVwap{
        sessionState.market_volume == 0 ? 0
        : static_cast<int64_t>((sessionState.market_notional + sessionState.market_volume / 2) / sessionState.market_volume)
    };
    std::println("session VWAP\t\t{}", sessionVwap);

    if (sessionState.executed_qty == 0 || sessionVwap == 0)
        std::println("slippage vs VWAP\tn/a");
    else {
        __int128 cost{
            (sessionState.buy_notional - sessionVwap * sessionState.buy_qty) + (sessionVwap * sessionState.sell_qty - sessionState.sell_notional)
        };
        double slippage{ static_cast<double>(cost * 10'000) / static_cast<double>(sessionVwap * (sessionState.buy_qty + sessionState.sell_qty)) };
        std::string favorabilityStr = slippage < 0 ? "favorable" : "unfavorable";
        std::println("slippage vs VWAP\t{:.2f} bps ({})", slippage, favorabilityStr);
    }

    std::println("participation\t\t{:.2f}% (cap {:.2f}%)",
        static_cast<double>(sessionState.executed_qty) / static_cast<double>(sessionState.market_volume) * 100.0,
        static_cast<double>(config.participation_cap_scaled) / 100.0
        );

    std::ranges::sort(tickToOrderTimes);
    auto n{tickToOrderTimes.size()};
    uint64_t p50{tickToOrderTimes[(n * 50 + 99) / 100 - 1]};
    uint64_t p99{tickToOrderTimes[(n * 99 + 99) / 100 - 1]};
    uint64_t p999{tickToOrderTimes[(n * 999 + 999) / 1000 - 1]};
    std::println("tick-to-order p50\t{} us", p50 / 1000.0);
    std::println("tick-to-order p99\t{} us", p99 / 1000.0);
    std::println("tick-to-order p99.9\t{} us", p999 / 1000.0);
}

static int createListeningSocket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        throw std::runtime_error("socket creation failed");

    int opt{1};
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind failed");

    listen(sockfd, 5);
    return sockfd;
}

int main(int argc, char *argv[]) {
    CLI::App app{"slipstream: volume-aware execution engine"};

    std::string symbol;
    app.add_option("--symbol", symbol, "symbol")->required();

    uint32_t maxQty;
    app.add_option("--max-quantity", maxQty, "max quantity")->required();

    std::string participationCapStr;
    app.add_option("--participation-cap", participationCapStr, "participation capacity")->required();

    uint64_t vwapWindow_ms;
    app.add_option("--vwap-window-ms", vwapWindow_ms, "vwap window ms")->required();

    std::string bandBpsStr;
    app.add_option("--band-bps", bandBpsStr, "band bps")->required();

    std::string mdHost;
    app.add_option("--md-host", mdHost, "md host")->required();
    int mdPort;
    app.add_option("--md-port", mdPort, "md port")->required();

    std::string oeHost;
    app.add_option("--oe-host", oeHost, "oe host")->required();
    int oePort;
    app.add_option("--oe-port", oePort, "oe port")->required();

    std::string transport;
    app.add_option("--transport", transport, "transport type")->required();

    CLI11_PARSE(app, argc, argv);
    if (getFixedPointPrecision(participationCapStr) > 4)
        throw std::runtime_error("participation cap should have 4 or fewer decimal digits, else it would be truncated");
    if (getFixedPointPrecision(bandBpsStr) > 4)
        throw std::runtime_error("band bps should have 4 or fewer decimal digits, else it would be truncated");
    uint64_t participationCap{parseFixedPoint(participationCapStr)};
    uint64_t bandBps{parseFixedPoint(bandBpsStr)};

    RollingVwap<VWAP_CAPACITY> vwap(vwapWindow_ms);
    L1State state{};
    const DecisionConfig config{
        .band_bps_scaled = bandBps,
        .participation_cap_scaled = participationCap,
        .max_qty = maxQty
    };
    SessionState sessionState{};

    int mdSockfd{createListeningSocket(mdPort)};
    int oeSockfd{createListeningSocket(oePort)};

    std::atomic mdClientConnected{false};
    std::atomic oeClientConnected{false};

    std::atomic open{true};

    std::jthread mdThread([&](std::stop_token st) {
        sockaddr_in clientAddr{};
        socklen_t len{sizeof(clientAddr)};

        std::println("Waiting for MD Client...");
        int clientSockfd{accept(mdSockfd, reinterpret_cast<sockaddr *>(&clientAddr), &len)};
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        std::println("Connected to MD Client");
        mdClientConnected.store(true, std::memory_order_relaxed);
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbol.data(), n, symbolArr.begin());

        while (!oeClientConnected.load(std::memory_order_relaxed)) // wait for OE client to connect
            std::this_thread::yield();

        serveMDClient(clientSockfd, symbolArr, state, std::move(st));
    });

    std::jthread oeThread([&](std::stop_token st) {
        sockaddr_in clientAddr{};
        socklen_t len{sizeof(clientAddr)};

        std::println("Waiting for OE Client...");
        int clientSockfd{accept(oeSockfd, reinterpret_cast<sockaddr *>(&clientAddr), &len)};
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        std::println("Connected to OE Client");
        oeClientConnected.store(true, std::memory_order_relaxed);
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbol.data(), n, symbolArr.begin());

        while (!mdClientConnected.load(std::memory_order_relaxed)) // wait for MD client to connect
            std::this_thread::yield();

        auto timeStart{std::chrono::steady_clock::now()};
        serveOEClient(clientSockfd, symbolArr, state, vwap, sessionState, config, timeStart, std::move(st), open);
    });

    std::thread consoleInputThread([&] {
        while (true) {
            std::string input{};
            std::cin >> input;

            if (input == "OPEN") {
                if (open.load(std::memory_order_relaxed))
                    std::println("Already open!");
                else {
                    std::println("Now open for new orders");
                    open.store(true, std::memory_order_relaxed);
                }
            } else if (input == "HALT") {
                if (!open.load(std::memory_order_relaxed))
                    std::println("Already halted!");
                else {
                    std::println("Halting new order messages...");
                    open.store(false, std::memory_order_relaxed);
                }
            } else if (input == "CLOSE") {
                std::println("Closing connection...");
                mdThread.request_stop();
                oeThread.request_stop();
                break;
            } else {
                std::println("Command not recognized");
            }
        }
    });

    mdThread.join();
    oeThread.join();
    consoleInputThread.join();

    close(mdSockfd);
    close(oeSockfd);
}
