#include "BytesParser.h"

#include "serialization/Decoder.h"

void processQuoteBytes(std::vector<std::byte>& accumBuf,
                       const std::span<const std::byte> incomingBytes,
                       const std::array<char, 12>& symbol,
                       L1State &state) {
    processBytes(accumBuf, incomingBytes, [&symbol, &state](const Message& msg) {
        if (const auto* q{ std::get_if<Quote>(&msg) }; q->symbol == symbol) {
            std::scoped_lock lock{state.stateMutex};
            state.bid_px = q->bid_px;
            state.bid_qty = q->bid_qty;
            state.ask_px = q->ask_px;
            state.ask_qty = q->ask_qty;
            ++state.quotesObserved;
        }
    });
}

void processTradeBytes(std::vector<std::byte>& accumBuf,
                       std::span<const std::byte> incomingBytes,
                       const std::array<char, 12>& symbol,
                       RollingVwap& rollingVwap) {
    processBytes(accumBuf, incomingBytes, [&symbol, &rollingVwap](const Message& msg) {
        if (const auto* t { std::get_if<Trade>(&msg) }; t->symbol == symbol)
            rollingVwap.addTick(t->ts_ns, t->px, t->qty);
    });
}
