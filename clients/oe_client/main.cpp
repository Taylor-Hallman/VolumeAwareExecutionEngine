#include "../../src/util/Overloaded.h"
#include "util/Parser.h"
#include "../../src/util/Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "serialization/Encoder.h"

int main(int argc, char* argv[]) {
    auto events{ parseQuotesAndTrades("Quotes_and_Trades.csv") };

    replayEvents(events, overloaded{
        [](const Quote&) {}, // ignore
        [](const Trade& t) {
            std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
            Encode(t, buf);


        }
    });
}
