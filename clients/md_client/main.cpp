#include "Overloaded.h"
#include "Parser.h"
#include "Replayer.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"
#include "serialization/Encoder.h"

int main() {
    auto events{ parseQuotesAndTrades("Quotes_and_Trades.csv") };

    replayEvents(events, overloaded{
        [](const Quote& q) {
            std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
            Encode(q, buf);
            // TODO: send buf over socket, M2
        },
        [](const Trade&) {} // ignore
    });
}
