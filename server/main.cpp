#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <mutex>
#include <print>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

#include <CLI/CLI.hpp>

#include "serialization/Decoder.h"
#include "util/BytesParser.h"

static bool serveMDClient(const int clientSockfd, const std::array<char, 12>& symbol, L1State& state) {
    bool success{ true };
    std::vector<std::byte> accumBuf;
    while (true) {
        std::array<std::byte, 1024> buf{};
        ssize_t bytesReceived = recv(clientSockfd, buf.data(), buf.size(), 0);
        std::println("Received {} bytes", bytesReceived);
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

static bool serveOEClient(const int clientSockfd, const std::array<char, 12>& symbol, RollingVwap<VWAP_CAPACITY>& vwap) {
    bool success{ true };
    std::vector<std::byte> accumBuf;
    while (true) {
        std::array<std::byte, 1024> buf{};
        ssize_t bytesReceived = recv(clientSockfd, buf.data(), buf.size(), 0);
        std::println("Received {} bytes", bytesReceived);
        if (bytesReceived <= 0) {
            if (bytesReceived < 0)
                success = false;
            break;
        }

        processTradeBytes(accumBuf, std::span(buf.begin(), bytesReceived), symbol, vwap);
    }
    close(clientSockfd);
    return success;
}

static int createListeningSocket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        throw std::runtime_error("socket creation failed");

    int opt{ 1 };
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind failed");

    listen(sockfd, 5);
    return sockfd;
}

int main (int argc, char* argv[]) {
    CLI::App app{"slipstream: volume-aware execution engine"};

    std::string symbol;
    app.add_option("--symbol", symbol, "symbol")->required();

    uint32_t maxQty;
    app.add_option("--max-quantity", maxQty, "max quantity")->required();

    double participationCap;
    app.add_option("--participation-cap", participationCap, "participation capacity")->required();

    uint64_t vwapWindow_ms;
    app.add_option("--vwap-window-ms", vwapWindow_ms, "vwap window ms")->required();

    double bandBps;
    app.add_option("--band-bps", bandBps, "band bps")->required();

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

    RollingVwap<VWAP_CAPACITY> vwap(vwapWindow_ms);
    L1State state;

    int mdSockfd{ createListeningSocket(mdPort) };
    int oeSockfd{ createListeningSocket(oePort) };

    std::thread mdThread([mdSockfd, &symbol, &state] {
        sockaddr_in clientAddr{};
        socklen_t len{ sizeof(clientAddr) };

        std::println("Waiting for MD Client...");
        int clientSockfd{ accept(mdSockfd, reinterpret_cast<sockaddr*>(&clientAddr), &len) };
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        std::println("Connected to MD Client");
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbolArr.data(), n, symbol.begin());
        serveMDClient(clientSockfd, symbolArr, state);
    });

    std::thread oeThread([oeSockfd, &symbol, &vwap] {
        sockaddr_in clientAddr{};
        socklen_t len{ sizeof(clientAddr) };

        std::println("Waiting for OE Client...");
        int clientSockfd{ accept(oeSockfd, reinterpret_cast<sockaddr*>(&clientAddr), &len) };
        if (clientSockfd < 0)
            throw std::runtime_error("accept failed");
        std::println("Connected to OE Client");
        std::array<char, 12> symbolArr{};
        size_t n = std::min(12uz, symbol.size());
        std::copy_n(symbolArr.data(), n, symbol.begin());
        serveOEClient(clientSockfd, symbolArr, vwap);
    });

    mdThread.join();
    oeThread.join();

    close(mdSockfd);
    close(oeSockfd);
}
