#include "../../src/util/Overloaded.h"
#include "util/Parser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"
#include "serialization/Encoder.h"

#include <CLI/CLI.hpp>

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

    replayEvents(events, overloaded{
        [&sockfd](const Quote& q) {
            std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
            Encode(q, buf);

            sendAll(sockfd, buf);
        },
        [](const Trade&) {} // ignore
    });

    close(sockfd);
}
