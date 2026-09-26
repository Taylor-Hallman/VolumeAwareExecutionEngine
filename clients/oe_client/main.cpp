#include "Overloaded.h"
#include "Parser.h"
#include "Replayer.h"
#include "serialization/Encoder.h"

int main() {
    auto events{ parseQuotesAndTrades("Quotes_and_Trades.csv") };

    replayEvents(events, overloaded{
        [](const Quote&) {}, // ignore
        [](const Trade& t) {
            std::vector<std::byte> buf(Quote::SIZE);
            Encode(t, buf);
            // TODO: send buf over socket, M2
        }
    });
}
