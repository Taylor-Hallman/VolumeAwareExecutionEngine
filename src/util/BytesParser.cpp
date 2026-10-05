#include "BytesParser.h"

#include "serialization/Decoder.h"

void processQuoteBytes(std::vector<std::byte>& accumBuf,
                       const std::span<const std::byte> incomingBytes,
                       const std::array<char, 12>& symbol,
                       L1State &state) {
    processBytes(accumBuf, incomingBytes, [&symbol, &state](const Message& msg) {
        if (const auto* q{ std::get_if<Quote>(&msg) }) {
            if (symbol != q->symbol)
                return;
            std::scoped_lock lock{state.stateMutex};
            state.bid_px = q->bid_px;
            state.bid_qty = q->bid_qty;
            state.ask_px = q->ask_px;
            state.ask_qty = q->ask_qty;
            ++state.quotesObserved;
        }
    });
}