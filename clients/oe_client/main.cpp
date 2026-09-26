#include "Overloaded.h"
#include "Parser.h"
#include "Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "serialization/Encoder.h"

int main() {
    auto events{ parseQuotesAndTrades("Quotes_and_Trades.csv") };

    replayEvents(events, overloaded{
        [](const Quote&) {}, // ignore
        [](const Trade& t) {
            std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
            Encode(t, buf);
            // TODO: send buf over socket, M2
        }
    });
}
