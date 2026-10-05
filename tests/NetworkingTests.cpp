#include "data/L1State.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Quote.h"
#include "gtest/gtest.h"
#include "serialization/Encoder.h"
#include "util/BytesParser.h"

TEST(NetworkingTest, AccumulatesAcrossFragmentedChunks) {
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

TEST(NetworkingTest, HandlesMultipleMessagesInOneChunk) {
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

TEST(NetworkingTest, IgnoresQuotesForOtherSymbols) {
    constexpr std::array<char, 12> mySymbol{'S','Y','N','T','H','1'};
    constexpr std::array<char, 12> otherSymbol{'S','Y','N','T','H','2'};
    L1State state{};
    std::vector<std::byte> accumBuf;

    constexpr Quote q{ .symbol = otherSymbol, .ts_ns = 1, .bid_qty = 999, .bid_px = 999'0000, .ask_qty = 999, .ask_px = 999'0500 };
    std::vector<std::byte> encoded(FrameHeader::SIZE + Quote::SIZE);
    Encode(q, std::span<std::byte>(encoded));

    processQuoteBytes(accumBuf, std::span(encoded), mySymbol, state);

    EXPECT_EQ(state.bid_px, 0) << "Quote for a different symbol should not update state";
}