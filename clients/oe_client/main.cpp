#include "../../src/util/Overloaded.h"
#include "util/CSVParser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/SessionControl.h"
#include "serialization/Encoder.h"
#include <print>

#include <CLI/CLI.hpp>
#include <sys/socket.h>

#include "serialization/Decoder.h"
#include "util/BytesParser.h"
#include "util/NetworkHelper.h"

int main(int argc, char *argv[]) {
    CLI::App app{"order-entry client"};

    std::string host;
    app.add_option("--host", host, "Host address")->required();

    int port;
    app.add_option("--port", port, "Port number")->required();

    std::string csv;
    app.add_option("--csv", csv, "CSV input file")->required();

    CLI11_PARSE(app, argc, argv);

    auto events{parseQuotesAndTrades(csv)};

    int sockfd{createSocket(host.data(), port)};

    std::atomic shutdownFlag{ false };

    std::thread receiver{
        [&] {
            std::vector<std::byte> accumBuf;
            while (!shutdownFlag.load(std::memory_order_relaxed)) {
                std::array<std::byte, 1024> buf{};

                ssize_t bytesReceived = recv(sockfd, buf.data(), buf.size(), 0);
                if (bytesReceived <= 0) {
                    if (bytesReceived < 0) {
                        if (errno == EINTR)
                            continue;
                        std::println("recv failed: {}", strerror(errno));
                        shutdownFlag.store(true, std::memory_order_relaxed);
                    }
                    break;
                }

                processBytes(accumBuf, std::span(buf.begin(), bytesReceived),
                    [&shutdownFlag](const Message& msg) {
                    if (const auto* hb{ std::get_if<Heartbeat>(&msg) }) {
                        std::println("Received heartbeat at timestamp {}", hb->ts_ns);
                    }
                    else if (const auto* newOrder{ std::get_if<NewOrder>(&msg) }) {
                        if (newOrder->status == 'A') {
                            std::string sideStr = newOrder->side == 'B' ? "Buy" : "Sell";
                            std::println("Trade {0} accepted at side {1}", newOrder->trade_id, sideStr);
                        }
                        else
                            std::println("Trade {} rejected", newOrder->trade_id);
                    }
                    else if (const auto* sc{ std::get_if<SessionControl>(&msg) }) {
                        switch (sc->state) {
                            case OPEN:
                                std::println("Received command: OPEN");
                                break;
                            case HALT:
                                std::println("Received command: HALT");
                                break;
                            case CLOSE:
                                std::println("Received command: CLOSE");
                                shutdownFlag.store(true, std::memory_order_relaxed);
                                break;
                            default:
                                std::println("Received unknown command");
                        }
                    }
                });
            }
        }
    };

    replayEvents(events, shutdownFlag, overloaded{
                     [](const Quote &) {
                     }, // ignore
                     [&sockfd, &shutdownFlag](const Trade &t) {
                         std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
                         Encode(t, buf);

                         if (!sendAll(sockfd, buf)) {
                             std::println("Could not connect to server. Closing...");
                             shutdownFlag.store(true, std::memory_order_relaxed);
                         }
                     }
                 });
    shutdownFlag.store(true, std::memory_order_relaxed);
    shutdown(sockfd, SHUT_RD); // wake receiver thread if it's waiting on recv
    receiver.join();
}
