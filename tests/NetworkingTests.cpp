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

TEST(OeServerTest, AccumulatesAcrossFragmentedChunks) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;

    const Trade t{
        .symbol = symbol,
        .ts_ns = 1,
        .qty = 175,
        .px = 101'2300,
        .aggressor = '?'
    };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Trade::SIZE);
    const size_t total = Encode(t, std::span(encoded));

    // Feed it in three arbitrarily-sized fragments, none of which align to
    // the message boundary, simulating worst-case TCP chunking.
    constexpr size_t chunk1 = 10;
    constexpr size_t chunk2 = 20;
    processTradeBytes(accumBuf, std::span(encoded.data(), chunk1), symbol, vwap);
    EXPECT_EQ(vwap.getTickCount(), 0) << "Should not have decoded yet with only " << chunk1 << " bytes";

    processTradeBytes(accumBuf, std::span(encoded.data() + chunk1, chunk2), symbol, vwap);
    EXPECT_EQ(vwap.getTickCount(), 0) << "Should not have decoded yet with only " << (chunk1 + chunk2) << " bytes";

    processTradeBytes(accumBuf, std::span(encoded.data() + chunk1 + chunk2, total - chunk1 - chunk2), symbol, vwap);
    EXPECT_EQ(vwap.getTickCount(), 1) << "Should have decoded once all bytes arrived";

    EXPECT_TRUE(accumBuf.empty()) << "Buffer should be fully drained after a complete decode";
}

TEST(OeServerTest, HandlesMultipleMessagesInOneChunk) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;

    const Trade t1{ .symbol = symbol, .ts_ns = 1, .qty = 100, .px = 100'0000, .aggressor = '?' };
    const Trade t2{ .symbol = symbol, .ts_ns = 2, .qty = 200, .px = 200'0000, .aggressor = '?' };

    std::vector<std::byte> buf1(FrameHeader::SIZE + Trade::SIZE);
    std::vector<std::byte> buf2(FrameHeader::SIZE + Trade::SIZE);
    Encode(t1, std::span(buf1));
    Encode(t2, std::span(buf2));

    std::vector<std::byte> combined;
    combined.insert(combined.end(), buf1.begin(), buf1.end());
    combined.insert(combined.end(), buf2.begin(), buf2.end());

    // Both messages arrive in a single "recv()" worth of bytes
    processTradeBytes(accumBuf, std::span(combined), symbol, vwap);

    // Vwap should contain both trades
    EXPECT_EQ(vwap.getTickCount(), 2);
    EXPECT_TRUE(accumBuf.empty()) << "Both messages should have been fully consumed";
}

TEST(OeServerTest, IgnoresQuotesForOtherSymbols) {
    constexpr std::array<char, 12> mySymbol{'S','Y','N','T','H','1'};
    constexpr std::array<char, 12> otherSymbol{'S','Y','N','T','H','2'};
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;

    const Trade t{ .symbol = otherSymbol, .ts_ns = 1, .qty = 999, .px = 999'0000, .aggressor = '?' };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Trade::SIZE);
    Encode(t, std::span(encoded));

    processTradeBytes(accumBuf, std::span(encoded), mySymbol, vwap);

    EXPECT_EQ(vwap.getTickCount(), 0) << "Trade for a different symbol should not update vwap";
}

TEST(OeServerTest, IgnoresNonTradeMessages) {
    constexpr std::array<char, 12> symbol{'S','Y','N','T','H','1'};
    RollingVwap<VWAP_CAPACITY> vwap(1000);
    std::vector<std::byte> accumBuf;

    constexpr Quote q{ .symbol = symbol, .ts_ns = 1, .bid_qty = 999, .bid_px = 999'0000, .ask_qty = 999, .ask_px = 999'0500 };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Quote::SIZE);
    Encode(q, std::span(encoded));

    processTradeBytes(accumBuf, std::span(encoded), symbol, vwap);
    EXPECT_EQ(vwap.getTickCount(), 0) << "Message that isn't a trade should not update state";
}
