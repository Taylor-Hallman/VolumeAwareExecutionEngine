#include "BytesParser.h"

#include "serialization/Decoder.h"

void processQuoteBytes(std::vector<std::byte>& accumBuf,
                       std::span<const std::byte> incomingBytes,
                       const std::array<char, 12>& symbol,
                       L1State& state,
                       std::mutex& stateMutex) {
    accumBuf.insert(accumBuf.end(), incomingBytes.begin(), incomingBytes.end());

    auto res{ Decode(std::span(accumBuf)) };
    while (res.message.has_value()) {
        Message msg{ res.message.value() };
        if (const Quote* quote{ std::get_if<Quote>(&msg) }) {
            if (quote->symbol == symbol) {
                std::scoped_lock lock{ stateMutex };
                state.bid_px = quote->bid_px;
                state.bid_qty = quote->bid_qty;
                state.ask_px = quote->ask_px;
                state.ask_qty = quote->ask_qty;
            }
        }
        accumBuf.erase(accumBuf.begin(), accumBuf.begin() + static_cast<ptrdiff_t>(res.bytesConsumed));
        res = Decode(std::span(accumBuf));
    }
}