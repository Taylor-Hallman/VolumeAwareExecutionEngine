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
    while (!st.stop_requested()) {
        std::array<std::byte, 1024> buf{};
        ssize_t bytesReceived = recv(clientSockfd, buf.data(), buf.size(), 0);
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                success = false;
            break;
        }

        processQuoteBytes(accumBuf, std::span(buf.begin(), bytesReceived), symbol, state);
    }
    close(clientSockfd);
    return success;
}

static bool serveOEClient(const int clientSockfd, const std::array<char, 12> &symbol, const L1State &l1State,
                          RollingVwap<VWAP_CAPACITY> &vwap, SessionState &sessionState,
                          const DecisionConfig &config, std::chrono::time_point<std::chrono::steady_clock> startTime,
                          std::stop_token st, std::atomic<bool> &open) {
    constexpr auto timeoutMs{5000};
    bool success{true};
    std::vector<std::byte> accumBuf;

    pollfd pfd{};
    pfd.fd = clientSockfd;
    pfd.events = POLLIN;

    bool isOpen{};

    while (true) {
        if (st.stop_requested()) {
            SessionControl sc{
                .ts_ns = getTimestamp(startTime),
                .state = CLOSE
            };
            std::vector<std::byte> bytes(FrameHeader::SIZE + SessionControl::SIZE);
            Encode(sc, bytes);
            if (!sendAll(clientSockfd, bytes))
                std::println("Failed to send close command to OE client");
            break;
        }

        if (const bool bl{ open.load(std::memory_order_relaxed) }; bl != isOpen) {
            const uint8_t state = bl ? OPEN : HALT;
            SessionControl sc{
                .ts_ns = getTimestamp(startTime),
                .state = state
            };
            std::vector<std::byte> bytes(FrameHeader::SIZE + SessionControl::SIZE);
            Encode(sc, bytes);
            if (!sendAll(clientSockfd, bytes)) {
                std::string stateStr = state == OPEN ? "open" : "halt";
                std::println("Failed to send {} command to OE client", stateStr);
            }
            isOpen = bl;
        }

        int ready{poll(&pfd, 1, timeoutMs)};

        if (ready < 0) {
            if (errno == EINTR) {
                std::println("interrupt");
                continue;
            }
            std::println("poll failed");
            break;
        }
        if (ready == 0) {
            // Timed out after 5 seconds; send a heartbeat
            std::println("Timed out, sending heartbeat");
            Heartbeat hb{.ts_ns = getTimestamp(startTime) };
            std::vector<std::byte> bytes(FrameHeader::SIZE + Heartbeat::SIZE);
            Encode(hb, bytes);
            if (!sendAll(clientSockfd, bytes))
                std::println("Failed to send heartbeat to OE client");
            continue;
        }

        std::array<std::byte, 1024> buf{};

        ssize_t bytesReceived = recv(clientSockfd, buf.data(), buf.size(), 0);
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                success = false;
            break;
        }
        L1Snapshot snapshot{};
        {
            std::scoped_lock lock(l1State.stateMutex);
            snapshot.best_ask_px = l1State.ask_px;
            snapshot.best_bid_px = l1State.bid_px;
            snapshot.quotesObserved = l1State.quotesObserved;
        }

        const auto timeElapsed{std::chrono::steady_clock::now() - startTime};
        auto newOrder = processTradeBytes(accumBuf, std::span(buf.begin(), bytesReceived), symbol, snapshot,
                                          sessionState, config, timeElapsed, vwap);

        if (newOrder) {
            if (isOpen) {
                std::println("Sending new order...");
                std::vector<std::byte> bytes(FrameHeader::SIZE + NewOrder::SIZE);
                Encode(newOrder.value(), bytes);
                if (!sendAll(clientSockfd, bytes))
                    std::println("Failed to send new order to OE client");
            }
            else
                std::println("No new order sent (halted)");
        }
    }
    close(clientSockfd);
    return success;
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
    L1State state;
    const DecisionConfig config{
        .band_bps_scaled = bandBps,
        .participation_cap_scaled = participationCap,
        .max_qty = maxQty
    };
    SessionState sessionState;

    int mdSockfd{createListeningSocket(mdPort)};
    int oeSockfd{createListeningSocket(oePort)};

    bool mdClientConnected{false};
    bool oeClientConnected{false};

    std::atomic open{true};

    std::jthread mdThread([&](std::stop_token st) {
        sockaddr_in clientAddr{};
        socklen_t len{sizeof(clientAddr)};

        std::println("Waiting for MD Client...");
        int clientSockfd{accept(mdSockfd, reinterpret_cast<sockaddr *>(&clientAddr), &len)};
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        std::println("Connected to MD Client");
        mdClientConnected = true;
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbol.data(), n, symbolArr.begin());

        while (!oeClientConnected); // wait for OE client to connect

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
        oeClientConnected = true;
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbol.data(), n, symbolArr.begin());

        while (!mdClientConnected); // wait for MD client to connect

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