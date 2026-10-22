#include "../../src/util/Overloaded.h"
#include "util/CSVParser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"
#include "serialization/Encoder.h"

#include <CLI/CLI.hpp>
#include <print>

#include "util/NetworkHelper.h"

int main(int argc, char* argv[]) {
    CLI::App app{"order-entry client"};

    std::string host;
    app.add_option("--host", host, "Host address")->required();

    int port;
    app.add_option("--port", port, "Port number")->required();

    std::string csv;
    app.add_option("--csv", csv, "CSV input file")->required();

    CLI11_PARSE(app, argc, argv);

    auto events{ parseQuotesAndTrades(csv) };

    int sockfd{ createSocket(host.data(), port) };

    std::atomic shutdownFlag{ false };

    replayEvents(events, shutdownFlag, overloaded{
        [&sockfd, &shutdownFlag](const Quote& q) {
            std::array<std::byte, FrameHeader::SIZE + Quote::SIZE> buf{};
            Encode(q, buf);

            if (!sendAll(sockfd, buf)) {
                std::println("Could not connect to server. Closing...");
                shutdownFlag.store(true, std::memory_order_relaxed);
            }
        },
        [](const Trade&) {} // ignore
    });

    close(sockfd);
}
