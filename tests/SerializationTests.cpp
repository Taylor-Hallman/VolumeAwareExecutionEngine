#include "Parser.h"
#include "data/GCMD_1/FrameHeader.h"
#include "data/GCMD_1/Heartbeat.h"
#include "gtest/gtest.h"
#include "serialization/Encoder.h"
#include "serialization/Decoder.h"

namespace {
    enum MessageTypeIdx : size_t {
        HEARTBEAT = 0uz,
        QUOTE,
        SESSION_CONTROL,
        TRADE,
        EXEC_REPORT,
        NEW_ORDER
    };
}

TEST(SerializationTest, EncodeDecode_Uint64_ZeroAndMax) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Heartbeat::SIZE);
    for (uint64_t val : {UINT64_C(0), UINT64_MAX}) {
        Heartbeat hb{ .ts_ns = val };
        Encode(hb, std::span(buf));
        auto result = Decode(std::span(buf));
        EXPECT_EQ(std::get<HEARTBEAT>(result.message.value()).ts_ns, val);
    }
}

TEST(SerializationTest, EncodeDecode_Uint32_ZeroAndMax) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
    for (uint32_t val : {0u, UINT32_MAX}) {
        Quote q{
            .symbol = {'S','Y','N','T','H','1'},
            .ts_ns = 1'000'000,
            .bid_qty = val,
            .bid_px = 100'0000,
            .ask_qty = 100,
            .ask_px = 101'0000
        };
        size_t written = Encode(q, std::span(buf));
        ASSERT_EQ(written, FrameHeader::SIZE + Quote::SIZE);

        DecodeResult result = Decode(std::span(buf));
        ASSERT_TRUE(result.message.has_value());
        EXPECT_EQ(std::get<Quote>(result.message.value()).bid_qty, val);
    }
}

TEST(SerializationTest, EncodeDecode_Int64_MinMaxAndNegative) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
    for (int64_t val : {INT64_MIN, INT64_MAX, int64_t{-123456}}) {
        Quote q{
            .symbol = {'S','Y','N','T','H','1'},
            .ts_ns = 1'000'000,
            .bid_qty = 100,
            .bid_px = val,
            .ask_qty = 100,
            .ask_px = 101'0000
        };
        size_t written = Encode(q, std::span(buf));
        ASSERT_EQ(written, FrameHeader::SIZE + Quote::SIZE);

        DecodeResult result = Decode(std::span(buf));
        ASSERT_TRUE(result.message.has_value());
        EXPECT_EQ(std::get<Quote>(result.message.value()).bid_px, val);
    }
}

TEST(SerializationTest, EncodeDecode_Uint8_ZeroAndMax) {
    std::vector<std::byte> buf(FrameHeader::SIZE + SessionControl::SIZE);
    for (uint8_t val : {uint8_t{0}, uint8_t{UINT8_MAX}}) {
        SessionControl sc{ .ts_ns = 1'000'000, .state = val };
        size_t written = Encode(sc, std::span(buf));
        ASSERT_EQ(written, FrameHeader::SIZE + SessionControl::SIZE);

        DecodeResult result = Decode(std::span(buf));
        ASSERT_TRUE(result.message.has_value());
        EXPECT_EQ(std::get<SessionControl>(result.message.value()).state, val);
    }
}

TEST(SerializationTest, EncodeDecode_Char_Boundaries) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
    for (char val : {'\0', 'B', 'S', '?', static_cast<char>(0x7F)}) {
        Trade t{
            .symbol = {'S','Y','N','T','H','1'},
            .ts_ns = 1'000'000,
            .qty = 100,
            .px = 100'0000,
            .aggressor = val,
            .id = 1
        };
        size_t written = Encode(t, std::span(buf));
        ASSERT_EQ(written, FrameHeader::SIZE + Trade::SIZE);

        DecodeResult result = Decode(std::span(buf));
        ASSERT_TRUE(result.message.has_value());
        EXPECT_EQ(std::get<Trade>(result.message.value()).aggressor, val);
    }
}

// --- Structural round-trip tests, one per message type ---
// These confirm each message type's fields are correctly encoded and decoded
// in the right order/position, with the frame header handled correctly.
// Field values are arbitrary/representative, not boundary-focused — value
// boundaries for shared field types are covered separately.

TEST(SerializationTest, RoundTripHeartbeat) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Heartbeat::SIZE);

    Heartbeat hb{ .ts_ns = 1'234'567'890 };
    size_t written = Encode(hb, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + Heartbeat::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + Heartbeat::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<Heartbeat>(result.message.value());
    EXPECT_EQ(decoded.ts_ns, hb.ts_ns);
}

TEST(SerializationTest, RoundTripQuote) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);

    Quote q{
        .symbol = {'S','Y','N','T','H','1'},
        .ts_ns = 1'234'567'890,
        .bid_qty = 175,
        .bid_px = 101'2300,
        .ask_qty = 150,
        .ask_px = 101'2500
    };
    size_t written = Encode(q, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + Quote::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + Quote::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<Quote>(result.message.value());
    EXPECT_EQ(decoded.symbol, q.symbol);
    EXPECT_EQ(decoded.ts_ns, q.ts_ns);
    EXPECT_EQ(decoded.bid_qty, q.bid_qty);
    EXPECT_EQ(decoded.bid_px, q.bid_px);
    EXPECT_EQ(decoded.ask_qty, q.ask_qty);
    EXPECT_EQ(decoded.ask_px, q.ask_px);
}

TEST(SerializationTest, RoundTripSessionControl) {
    std::vector<std::byte> buf(FrameHeader::SIZE + SessionControl::SIZE);

    SessionControl sc{ .ts_ns = 1'234'567'890, .state = 1 }; // HALT
    size_t written = Encode(sc, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + SessionControl::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + SessionControl::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<SessionControl>(result.message.value());
    EXPECT_EQ(decoded.ts_ns, sc.ts_ns);
    EXPECT_EQ(decoded.state, sc.state);
}

TEST(SerializationTest, RoundTripTrade) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);

    Trade t{
        .symbol = {'S','Y','N','T','H','2'},
        .ts_ns = 1'234'567'890,
        .qty = 65,
        .px = 248'5300,
        .aggressor = 'B',
        .id = 42
    };
    size_t written = Encode(t, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + Trade::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + Trade::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<Trade>(result.message.value());
    EXPECT_EQ(decoded.symbol, t.symbol);
    EXPECT_EQ(decoded.ts_ns, t.ts_ns);
    EXPECT_EQ(decoded.qty, t.qty);
    EXPECT_EQ(decoded.px, t.px);
    EXPECT_EQ(decoded.aggressor, t.aggressor);
    EXPECT_EQ(decoded.id, t.id);
}

TEST(SerializationTest, RoundTripNewOrder) {
    std::vector<std::byte> buf(FrameHeader::SIZE + NewOrder::SIZE);

    NewOrder no{
        .client_order_id = 7,
        .symbol = {'S','Y','N','T','H','1'},
        .status = 'A',
        .ts_ns = 1'234'567'890,
        .trade_id = 42,
        .side = 'B',
        .qty = 65,
        .limit_px = 101'2500
    };
    size_t written = Encode(no, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + NewOrder::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + NewOrder::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<NewOrder>(result.message.value());
    EXPECT_EQ(decoded.client_order_id, no.client_order_id);
    EXPECT_EQ(decoded.symbol, no.symbol);
    EXPECT_EQ(decoded.status, no.status);
    EXPECT_EQ(decoded.ts_ns, no.ts_ns);
    EXPECT_EQ(decoded.trade_id, no.trade_id);
    EXPECT_EQ(decoded.side, no.side);
    EXPECT_EQ(decoded.qty, no.qty);
    EXPECT_EQ(decoded.limit_px, no.limit_px);
}

TEST(SerializationTest, RoundTripExecReport) {
    std::vector<std::byte> buf(FrameHeader::SIZE + ExecReport::SIZE);

    ExecReport er{
        .client_order_id = 7,
        .ts_ns = 1'234'567'890,
        .status = 1, // FILL
        .filled_qty = 65,
        .avg_px = 101'2500,
        .reason_code = 0
    };
    size_t written = Encode(er, std::span(buf));
    ASSERT_EQ(written, FrameHeader::SIZE + ExecReport::SIZE);

    DecodeResult result = Decode(std::span(buf));
    ASSERT_EQ(result.bytesConsumed, FrameHeader::SIZE + ExecReport::SIZE);
    ASSERT_TRUE(result.message.has_value());

    auto decoded = std::get<ExecReport>(result.message.value());
    EXPECT_EQ(decoded.client_order_id, er.client_order_id);
    EXPECT_EQ(decoded.ts_ns, er.ts_ns);
    EXPECT_EQ(decoded.status, er.status);
    EXPECT_EQ(decoded.filled_qty, er.filled_qty);
    EXPECT_EQ(decoded.avg_px, er.avg_px);
    EXPECT_EQ(decoded.reason_code, er.reason_code);
}

// --- Split-frame / streaming tests ---
// These confirm Decode correctly handles buffers that don't align to message
// boundaries — the core requirement behind TCP's lack of message framing.

TEST(SerializationTest, Decode_TruncatedBuffer_NeverFalsePositive_Heartbeat) {
    std::vector<std::byte> full(FrameHeader::SIZE + Heartbeat::SIZE);
    Heartbeat hb{ .ts_ns = 1'234'567'890 };
    size_t total = Encode(hb, std::span(full));
    ASSERT_EQ(total, full.size());

    // Every truncated length short of the full message must report "incomplete"
    for (size_t len = 0; len < total; ++len) {
        DecodeResult result = Decode(std::span<const std::byte>(full.data(), len));
        EXPECT_FALSE(result.message.has_value())
            << "False positive decode at truncated length " << len;
        EXPECT_EQ(result.bytesConsumed, 0uz)
            << "Non-zero bytesConsumed on incomplete decode at length " << len;
    }

    // Exact full length must succeed
    DecodeResult result = Decode(std::span<const std::byte>(full.data(), total));
    ASSERT_TRUE(result.message.has_value());
    EXPECT_EQ(result.bytesConsumed, total);
}

TEST(SerializationTest, Decode_TruncatedBuffer_NeverFalsePositive_Quote) {
    // Quote has a symbol field, exercising a different decode path than Heartbeat
    std::vector<std::byte> full(FrameHeader::SIZE + Quote::SIZE);
    Quote q{
        .symbol = {'S','Y','N','T','H','1'},
        .ts_ns = 1'234'567'890,
        .bid_qty = 175,
        .bid_px = 101'2300,
        .ask_qty = 150,
        .ask_px = 101'2500
    };
    size_t total = Encode(q, std::span(full));
    ASSERT_EQ(total, full.size());

    for (size_t len = 0; len < total; ++len) {
        DecodeResult result = Decode(std::span<const std::byte>(full.data(), len));
        EXPECT_FALSE(result.message.has_value())
            << "False positive decode at truncated length " << len;
        EXPECT_EQ(result.bytesConsumed, 0uz)
            << "Non-zero bytesConsumed on incomplete decode at length " << len;
    }

    DecodeResult result = Decode(std::span<const std::byte>(full.data(), total));
    ASSERT_TRUE(result.message.has_value());
    EXPECT_EQ(result.bytesConsumed, total);
}

TEST(SerializationTest, Decode_ByteAtATimeGrowth_Heartbeat) {
    std::vector<std::byte> full(FrameHeader::SIZE + Heartbeat::SIZE);
    Heartbeat hb{ .ts_ns = 42 };
    size_t total = Encode(hb, std::span(full));

    // Simulate worst-case fragmentation: buffer grows one byte per "recv()"
    for (size_t len = 0; len < total; ++len) {
        DecodeResult result = Decode(std::span<const std::byte>(full.data(), len));
        ASSERT_FALSE(result.message.has_value())
            << "Should not decode successfully with only " << len << " of " << total << " bytes";
    }
    DecodeResult result = Decode(std::span<const std::byte>(full.data(), total));
    ASSERT_TRUE(result.message.has_value());
}

TEST(SerializationTest, Decode_MultipleMessagesInOneBuffer) {
    std::vector<std::byte> hbBuf(FrameHeader::SIZE + Heartbeat::SIZE);
    std::vector<std::byte> quoteBuf(FrameHeader::SIZE + Quote::SIZE);

    Heartbeat hb{ .ts_ns = 111 };
    size_t hbLen = Encode(hb, std::span<std::byte>(hbBuf));

    Quote q{
        .symbol = {'S','Y','N','T','H','1'},
        .ts_ns = 222,
        .bid_qty = 175,
        .bid_px = 101'2300,
        .ask_qty = 150,
        .ask_px = 101'2500
    };
    size_t quoteLen = Encode(q, std::span(quoteBuf));

    // Concatenate: Heartbeat immediately followed by Quote
    std::vector<std::byte> combined;
    combined.insert(combined.end(), hbBuf.begin(), hbBuf.begin() + hbLen);
    combined.insert(combined.end(), quoteBuf.begin(), quoteBuf.begin() + quoteLen);

    // First decode call should find only the Heartbeat
    DecodeResult first = Decode(std::span<const std::byte>(combined));
    ASSERT_TRUE(first.message.has_value());
    EXPECT_EQ(first.bytesConsumed, hbLen);
    auto decodedHb = std::get<Heartbeat>(first.message.value());
    EXPECT_EQ(decodedHb.ts_ns, hb.ts_ns);

    // Second decode call, on the remainder, should find the Quote
    DecodeResult second = Decode(std::span<const std::byte>(combined).subspan(first.bytesConsumed));
    ASSERT_TRUE(second.message.has_value());
    EXPECT_EQ(second.bytesConsumed, quoteLen);
    auto decodedQuote = std::get<Quote>(second.message.value());
    EXPECT_EQ(decodedQuote.ts_ns, q.ts_ns);
    EXPECT_EQ(decodedQuote.bid_px, q.bid_px);

    // No bytes should remain
    EXPECT_EQ(first.bytesConsumed + second.bytesConsumed, combined.size());
}

TEST(SerializationTest, Decode_MessagePlusPartialNextMessage) {
    // A buffer containing one complete message plus the start (but not all)
    // of a second message — the most realistic single-recv() scenario.
    std::vector<std::byte> hbBuf(FrameHeader::SIZE + Heartbeat::SIZE);
    Heartbeat hb{ .ts_ns = 111 };
    size_t hbLen = Encode(hb, std::span<std::byte>(hbBuf));

    std::vector<std::byte> quoteBuf(FrameHeader::SIZE + Quote::SIZE);
    Quote q{
        .symbol = {'S','Y','N','T','H','1'},
        .ts_ns = 222,
        .bid_qty = 175,
        .bid_px = 101'2300,
        .ask_qty = 150,
        .ask_px = 101'2500
    };
    Encode(q, std::span<std::byte>(quoteBuf));

    std::vector<std::byte> combined;
    combined.insert(combined.end(), hbBuf.begin(), hbBuf.begin() + hbLen);
    // Only append the first half of the Quote's bytes
    combined.insert(combined.end(), quoteBuf.begin(), quoteBuf.begin() + quoteBuf.size() / 2);

    DecodeResult first = Decode(std::span<const std::byte>(combined));
    ASSERT_TRUE(first.message.has_value());
    EXPECT_EQ(first.bytesConsumed, hbLen);

    // Remainder is a partial Quote — must report incomplete, not fail/crash
    DecodeResult second = Decode(std::span<const std::byte>(combined).subspan(first.bytesConsumed));
    EXPECT_FALSE(second.message.has_value());
    EXPECT_EQ(second.bytesConsumed, 0uz);
}

// --- Malformed / adversarial input tests ---

TEST(SerializationTest, Decode_UnrecognizedMsgType_Throws) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Heartbeat::SIZE);
    Heartbeat hb{ .ts_ns = 1 };
    Encode(hb, std::span<std::byte>(buf));

    // Corrupt msg_type (byte index 2 in the frame header) to an unknown value
    buf[2] = static_cast<std::byte>(99);

    EXPECT_THROW(Decode(std::span<const std::byte>(buf)), std::invalid_argument);
}

TEST(SerializationTest, Decode_MismatchedBodyLen_DoesNotMisparse) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
    Quote q{
        .symbol = {'S','Y','N','T','H','1'},
        .ts_ns = 1,
        .bid_qty = 1,
        .bid_px = 1,
        .ask_qty = 1,
        .ask_px = 1
    };
    Encode(q, std::span<std::byte>(buf));

    // Corrupt body_len (bytes 0-1, little-endian) even though the buffer
    // still contains a full, otherwise-valid Quote body
    buf[0] = static_cast<std::byte>(10);
    buf[1] = static_cast<std::byte>(0);

    DecodeResult result = Decode(std::span<const std::byte>(buf));
    EXPECT_FALSE(result.message.has_value());
    EXPECT_EQ(result.bytesConsumed, 0uz);
}

TEST(SerializationTest, Decode_EmptyBuffer_ReportsIncomplete) {
    std::vector<std::byte> empty;
    DecodeResult result = Decode(std::span<const std::byte>(empty));
    EXPECT_FALSE(result.message.has_value());
    EXPECT_EQ(result.bytesConsumed, 0uz);
}

TEST(SerializationTest, Decode_LessThanFrameHeader_ReportsIncomplete) {
    std::array<std::byte, 3> tooShort{}; // FrameHeader::SIZE is 4
    DecodeResult result = Decode(std::span<const std::byte>(tooShort));
    EXPECT_FALSE(result.message.has_value());
    EXPECT_EQ(result.bytesConsumed, 0uz);
}

// --- CSV parser tests ---

TEST(ParserTest, FixedPointPrice_TwoDecimalDigits) {
    EXPECT_EQ(parseFixedPointPrice("87.37"), 873700);
}

TEST(ParserTest, FixedPointPrice_WholeNumberNoDecimal) {
    EXPECT_EQ(parseFixedPointPrice("100"), 1000000);
}

TEST(ParserTest, FixedPointPrice_FourDecimalDigits) {
    EXPECT_EQ(parseFixedPointPrice("101.2500"), 1012500);
}

TEST(ParserTest, FixedPointPrice_SingleDecimalDigit) {
    EXPECT_EQ(parseFixedPointPrice("34.2"), 342000);
}

TEST(ParserTest, TimeToNsSinceOpen_ExactlyAnchor) {
    EXPECT_EQ(parseTimeToNsSinceOpen("09:30:00.000"), 0uz);
}

TEST(ParserTest, TimeToNsSinceOpen_SmallOffset) {
    // 500ms after open = 500,000,000 ns
    EXPECT_EQ(parseTimeToNsSinceOpen("09:30:00.500"), 500'000'000uz);
}

TEST(ParserTest, TimeToNsSinceOpen_CrossesHourAndMinuteBoundary) {
    // 09:30:00.000 -> 10:31:15.250
    // delta: 1h 1m 15.250s = 3675.250s = 3,675,250,000,000 ns
    EXPECT_EQ(parseTimeToNsSinceOpen("10:31:15.250"), 3'675'250'000'000uz);
}

TEST(ParserTest, ParseQuotesAndTrades_EndToEnd) {
    auto data = parseQuotesAndTrades("Quotes_and_Trades.csv");

    ASSERT_FALSE(data.empty());

    // Spot-check the first row against known sample data
    ASSERT_TRUE(std::holds_alternative<Quote>(data[0]));
    auto& firstQuote = std::get<Quote>(data[0]);
    EXPECT_EQ(std::string(firstQuote.symbol.data(), 6), "SYNTH3");
}