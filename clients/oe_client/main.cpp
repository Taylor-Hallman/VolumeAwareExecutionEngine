#include "../../src/util/Overloaded.h"
#include "util/CSVParser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
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

    bool shutdown{ false };

    std::thread receiver{
        [&] {
            std::vector<std::byte> accumBuf;
            while (!shutdown) {
                std::array<std::byte, 1024> buf{};

                ssize_t bytesReceived = recv(sockfd, buf.data(), buf.size(), 0);
                if (bytesReceived <= 0) {
                    if (bytesReceived < 0) {
                        // error
                    }
                    break;
                }

                processBytes(accumBuf, std::span(buf.begin(), bytesReceived),
                    [](const Message& msg) {
                    if (const auto* hb{ std::get_if<Heartbeat>(&msg) }) {
                        std::println("[OE CLIENT] Received heartbeat at timestamp {}", hb->ts_ns);
                    }
                    else if (const auto* newOrder{ std::get_if<NewOrder>(&msg) }) {
                        std::string statusStr = newOrder->status == 'A' ? "accepted" : "rejected";
                        std::println("[OE CLIENT] Received Order: Trade {0} {1}", newOrder->trade_id, statusStr);
                    }
                });
            }
        }
    };

    replayEvents(events, overloaded{
                     [](const Quote &) {
                     }, // ignore
                     [&sockfd](const Trade &t) {
                         std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
                         Encode(t, buf);

                         sendAll(sockfd, buf);
                     }
                 });
    shutdown = true;
    receiver.join();
}
