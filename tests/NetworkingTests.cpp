#include <chrono>

#include "data/L1State.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Quote.h"
#include "gtest/gtest.h"
#include "serialization/Encoder.h"
#include "util/BytesParser.h"

TEST(MdServerTest, AccumulatesAcrossFragmentedChunks) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    L1State state{};
    std::vector<std::byte> accumBuf;

    Quote q{
        .symbol = symbol,
        .ts_ns = 1,
        .bid_qty = 175,
        .bid_px = 101'2300,
        .ask_qty = 150,
        .ask_px = 101'2500
    };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Quote::SIZE);
    const size_t total = Encode(q, std::span(encoded));

    // Feed it in three arbitrarily-sized fragments, none of which align to
    // the message boundary, simulating worst-case TCP chunking.
    constexpr size_t chunk1 = 10;
    constexpr size_t chunk2 = 20;
    processQuoteBytes(accumBuf, std::span(encoded.data(), chunk1), symbol, state);
    EXPECT_EQ(state.bid_px, 0) << "Should not have decoded yet with only " << chunk1 << " bytes";

    processQuoteBytes(accumBuf, std::span(encoded.data() + chunk1, chunk2), symbol, state);
    EXPECT_EQ(state.bid_px, 0) << "Should not have decoded yet with only " << (chunk1 + chunk2) << " bytes";

    processQuoteBytes(accumBuf, std::span(encoded.data() + chunk1 + chunk2, total - chunk1 - chunk2), symbol, state);
    EXPECT_EQ(state.bid_px, q.bid_px) << "Should have decoded once all bytes arrived";
    EXPECT_EQ(state.bid_qty, q.bid_qty);
    EXPECT_EQ(state.ask_px, q.ask_px);
    EXPECT_EQ(state.ask_qty, q.ask_qty);

    EXPECT_TRUE(accumBuf.empty()) << "Buffer should be fully drained after a complete decode";
}

TEST(MdServerTest, HandlesMultipleMessagesInOneChunk) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    L1State state{};
    std::vector<std::byte> accumBuf;

    constexpr Quote q1{ .symbol = symbol, .ts_ns = 1, .bid_qty = 100, .bid_px = 100'0000, .ask_qty = 100, .ask_px = 100'0500 };
    constexpr Quote q2{ .symbol = symbol, .ts_ns = 2, .bid_qty = 200, .bid_px = 200'0000, .ask_qty = 200, .ask_px = 200'0500 };

    std::vector<std::byte> buf1(FrameHeader::SIZE + Quote::SIZE);
    std::vector<std::byte> buf2(FrameHeader::SIZE + Quote::SIZE);
    Encode(q1, std::span<std::byte>(buf1));
    Encode(q2, std::span<std::byte>(buf2));

    std::vector<std::byte> combined;
    combined.insert(combined.end(), buf1.begin(), buf1.end());
    combined.insert(combined.end(), buf2.begin(), buf2.end());

    // Both messages arrive in a single "recv()" worth of bytes
    processQuoteBytes(accumBuf, std::span(combined), symbol, state);

    // State should reflect the SECOND (most recent) quote, since it overwrites the first
    EXPECT_EQ(state.bid_px, q2.bid_px);
    EXPECT_EQ(state.bid_qty, q2.bid_qty);
    EXPECT_TRUE(accumBuf.empty()) << "Both messages should have been fully consumed";
}

TEST(MdServerTest, IgnoresQuotesForOtherSymbols) {
    constexpr std::array<char, 12> mySymbol{'S','Y','N','T','H','1'};
    constexpr std::array<char, 12> otherSymbol{'S','Y','N','T','H','2'};
    L1State state{};
    std::vector<std::byte> accumBuf;

    constexpr Quote q{ .symbol = otherSymbol, .ts_ns = 1, .bid_qty = 999, .bid_px = 999'0000, .ask_qty = 999, .ask_px = 999'0500 };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Quote::SIZE);
    Encode(q, std::span(encoded));

    processQuoteBytes(accumBuf, std::span(encoded), mySymbol, state);

    EXPECT_EQ(state.bid_px, 0) << "Quote for a different symbol should not update state";
}

TEST(MdServerTest, IgnoresNonQuoteMessages) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    L1State state{};
    std::vector<std::byte> accumBuf;

    const Trade t{ .symbol = symbol, .ts_ns = 1, .qty = 999, .px = 999'0000, .aggressor = '?'};
    std::vector<std::byte> encoded(FrameHeader::SIZE + Trade::SIZE);
    Encode(t, std::span(encoded));

    processQuoteBytes(accumBuf, std::span(encoded), symbol, state);
    EXPECT_EQ(state.bid_px, 0) << "Message that isn't a quote should not update state";
}

namespace {

constexpr std::array<char, 12> SYM1{'S','Y','N','T','H','1'};
constexpr std::array<char, 12> SYM2{'S','Y','N','T','H','2'};

// Feeds one chunk through processTradeBytes with the warm-up gate closed
// (default snapshot has 0 quotes; elapsed is 1 ns, far below the window).
// Returns how many orders reached the callback.
size_t feed(std::vector<std::byte>& accumBuf,
            std::span<const std::byte> bytes,
            const std::array<char, 12>& symbol,
            SessionState& sessionState,
            RollingVwap<VWAP_CAPACITY>& vwap) {
    using namespace std::chrono;
    size_t ordersDelivered = 0;
    processTradeBytes(accumBuf, bytes, symbol, L1Snapshot{}, sessionState, DecisionConfig{},
                      1ns, vwap, [&ordersDelivered](const NewOrder&) { ++ordersDelivered; });
    return ordersDelivered;
}

} // namespace

TEST(OeServerTest, AccumulatesAcrossFragmentedChunks) {
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;
    SessionState sessionState{};

    const Trade t{ .symbol = SYM1, .ts_ns = 1, .qty = 175, .px = 101'2300, .aggressor = '?' };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Trade::SIZE);
    const size_t total = Encode(t, std::span(encoded));

    // Three fragments, none aligned to the message boundary (worst-case TCP chunking).
    constexpr size_t chunk1 = 10;
    constexpr size_t chunk2 = 20;

    feed(accumBuf, std::span(encoded.data(), chunk1), SYM1, sessionState, vwap);
    EXPECT_EQ(vwap.getTickCount(), 0uz) << "Should not have decoded yet with only " << chunk1 << " bytes";
    EXPECT_EQ(sessionState.market_volume, 0u);

    feed(accumBuf, std::span(encoded.data() + chunk1, chunk2), SYM1, sessionState, vwap);
    EXPECT_EQ(vwap.getTickCount(), 0uz) << "Should not have decoded yet with only " << (chunk1 + chunk2) << " bytes";
    EXPECT_EQ(sessionState.market_volume, 0u);

    const size_t orders = feed(accumBuf, std::span(encoded.data() + chunk1 + chunk2, total - chunk1 - chunk2),
                               SYM1, sessionState, vwap);
    EXPECT_EQ(vwap.getTickCount(), 1uz) << "Should have decoded once all bytes arrived";
    EXPECT_EQ(sessionState.market_volume, 175u);
    EXPECT_EQ(orders, 0uz) << "Warm-up gate is closed, so no order is delivered";
    EXPECT_TRUE(accumBuf.empty()) << "Buffer should be fully drained after a complete decode";
}

TEST(OeServerTest, HandlesMultipleMessagesInOneChunk) {
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;
    SessionState sessionState{};

    const Trade t1{ .symbol = SYM1, .ts_ns = 1, .qty = 100, .px = 100'0000, .aggressor = '?' };
    const Trade t2{ .symbol = SYM1, .ts_ns = 2, .qty = 200, .px = 200'0000, .aggressor = '?' };

    std::vector<std::byte> buf1(FrameHeader::SIZE + Trade::SIZE);
    std::vector<std::byte> buf2(FrameHeader::SIZE + Trade::SIZE);
    Encode(t1, std::span(buf1));
    Encode(t2, std::span(buf2));

    std::vector<std::byte> combined;
    combined.insert(combined.end(), buf1.begin(), buf1.end());
    combined.insert(combined.end(), buf2.begin(), buf2.end());

    // Both messages arrive in a single "recv()" worth of bytes
    const size_t orders = feed(accumBuf, std::span(combined), SYM1, sessionState, vwap);

    EXPECT_EQ(vwap.getTickCount(), 2uz);
    EXPECT_EQ(sessionState.market_volume, 300u);
    EXPECT_EQ(orders, 0uz);
    EXPECT_TRUE(accumBuf.empty()) << "Both messages should have been fully consumed";
}

TEST(OeServerTest, IgnoresTradesForOtherSymbols) {
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;
    SessionState sessionState{};

    const Trade t{ .symbol = SYM2, .ts_ns = 1, .qty = 999, .px = 999'0000, .aggressor = '?' };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Trade::SIZE);
    Encode(t, std::span(encoded));

    feed(accumBuf, std::span(encoded), SYM1, sessionState, vwap);

    EXPECT_EQ(vwap.getTickCount(), 0uz) << "Trade for a different symbol should not update the VWAP";
    EXPECT_EQ(sessionState.market_volume, 0u) << "...or the market volume";
    EXPECT_TRUE(accumBuf.empty()) << "The message is consumed even though it's filtered out";
}

TEST(OeServerTest, IgnoresNonTradeMessages) {
    // (Prints a "[WARN] Expected Trade" line to stdout; that's expected.)
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;
    SessionState sessionState{};

    constexpr Quote q{ .symbol = SYM1, .ts_ns = 1, .bid_qty = 999, .bid_px = 999'0000,
                       .ask_qty = 999, .ask_px = 999'0500 };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Quote::SIZE);
    Encode(q, std::span(encoded));

    feed(accumBuf, std::span(encoded), SYM1, sessionState, vwap);

    EXPECT_EQ(vwap.getTickCount(), 0uz) << "Message that isn't a trade should not update state";
    EXPECT_EQ(sessionState.market_volume, 0u);
    EXPECT_TRUE(accumBuf.empty());
}