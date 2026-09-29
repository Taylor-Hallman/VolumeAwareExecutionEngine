#include "../../src/util/Overloaded.h"
#include "util/Parser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "serialization/Encoder.h"

#include <CLI/CLI.hpp>

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

    replayEvents(events, overloaded{
        [](const Quote&) {}, // ignore
        [](const Trade& t) {
            std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
            Encode(t, buf);


        }
    });
}
