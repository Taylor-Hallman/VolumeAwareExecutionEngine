#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "data/GCMD_1/FrameHeader.h"
#include "decision_engine/DecisionConfig.h"
#include "decision_engine/DecisionEngine.h"
#include "decision_engine/L1Snapshot.h"
#include "decision_engine/SessionState.h"
#include "serialization/Encoder.h"
#include "util/BytesParser.h"

namespace {

// --- Reference scenario: VWAP = 100.0000, band = 25.5 bps, cap = 15% ---
// Buy threshold  = 100 * (1 - 0.00255) =  99.7450 -> ask <= 997'450
// Sell threshold = 100 * (1 + 0.00255) = 100.2550 -> bid >= 1'002'550
constexpr int64_t  VWAP    = 1'000'000;
constexpr uint64_t BAND    = 255'000;   // 25.5 bps, scaled x10,000
constexpr uint64_t CAP     = 1'500;     // 0.15, scaled x10,000
constexpr uint32_t MAX_QTY = 500;
const std::array<char, 12> SYM{'S','Y','N','T','H','1'};
const std::array<char, 12> OTHER_SYM{'S','Y','N','T','H','2'};

DecisionConfig makeConfig(uint64_t band = BAND) {
    return DecisionConfig{ .band_bps_scaled = band,
                           .participation_cap_scaled = CAP,
                           .max_qty = MAX_QTY };
}

// Large market volume by default so the participation cap isn't the limiting factor
SessionState makeState(uint64_t marketVolume = 1'000'000, uint64_t executed = 0) {
    SessionState s{};
    s.market_volume = marketVolume;
    s.executed_qty = executed;
    return s;
}

L1Snapshot makeSnapshot(int64_t bid, int64_t ask, int quotes = 10) {
    L1Snapshot s{};
    s.best_bid_px = bid;
    s.best_ask_px = ask;
    s.quotesObserved = quotes;
    return s;
}

// Quotes relative to the reference scenario
const L1Snapshot NEUTRAL   = makeSnapshot(999'900,   1'000'100); // inside the band
const L1Snapshot BUY_QUOTE = makeSnapshot(996'900,     997'000); // ask 99.70 < 99.745
const L1Snapshot SELL_QUOTE= makeSnapshot(1'003'000, 1'003'100); // bid 100.30 > 100.255

Trade makeTrade(uint32_t qty, int64_t id = 1, int64_t px = VWAP,
                uint64_t ts = 5'000'000, std::array<char, 12> sym = SYM) {
    Trade t{};
    t.symbol = sym;
    t.ts_ns = ts;
    t.qty = qty;
    t.px = px;
    t.aggressor = '?';
    t.id = id;
    return t;
}

NewOrder judge(const L1Snapshot& snap, uint32_t qty = 100,
               const SessionState& state = makeState(),
               const DecisionConfig& cfg = makeConfig(), int64_t vwap = VWAP) {
    return judgeTrade(makeTrade(qty, 42), snap, state, cfg, vwap);
}

std::vector<std::byte> encodeTrade(const Trade& t) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Trade::SIZE);
    Encode(t, std::span<std::byte>(buf));
    return buf;
}

std::vector<std::byte> encodeQuote(const Quote& q) {
    std::vector<std::byte> buf(FrameHeader::SIZE + Quote::SIZE);
    Encode(q, std::span<std::byte>(buf));
    return buf;
}

const std::chrono::nanoseconds WINDOW = std::chrono::milliseconds{10};

} // namespace

// ============================== judgeTrade ==============================

TEST(JudgeTradeTest, AcceptsBuy_WhenAskBelowLowerBand) {
    NewOrder o = judge(BUY_QUOTE);
    EXPECT_EQ(o.status, 'A');
    EXPECT_EQ(o.side, 'B');
    EXPECT_EQ(o.limit_px, 997'000);   // buys cross at the best ask
    EXPECT_EQ(o.qty, 100u);
    EXPECT_EQ(o.trade_id, 42);
    EXPECT_EQ(o.symbol, SYM);
}

TEST(JudgeTradeTest, AcceptsSell_WhenBidAboveUpperBand) {
    NewOrder o = judge(SELL_QUOTE);
    EXPECT_EQ(o.status, 'A');
    EXPECT_EQ(o.side, 'S');
    EXPECT_EQ(o.limit_px, 1'003'000); // sells cross at the best bid
    EXPECT_EQ(o.qty, 100u);
    EXPECT_EQ(o.trade_id, 42);
}

TEST(JudgeTradeTest, Rejects_WhenNeitherTriggerFires_AndEchoesTradeFields) {
    NewOrder o = judge(NEUTRAL);
    EXPECT_EQ(o.status, 'R');
    EXPECT_EQ(o.trade_id, 42);
    EXPECT_EQ(o.qty, 100u);
    EXPECT_EQ(o.symbol, SYM);
    // side/limit_px on rejections are intentionally unspecified, so they're not asserted
}

TEST(JudgeTradeTest, Rejects_WhenAskBelowVwapButInsideBand) {
    // The spec's own example: below VWAP, but not by enough
    NewOrder o = judge(makeSnapshot(997'900, 998'000)); // ask 99.80
    EXPECT_EQ(o.status, 'R');
}

TEST(JudgeTradeTest, BuyTriggerBoundary_ExactThresholdAcceptsOneTickAboveRejects) {
    EXPECT_EQ(judge(makeSnapshot(997'350, 997'450)).status, 'A'); // exactly 99.7450: <= holds
    EXPECT_EQ(judge(makeSnapshot(997'351, 997'451)).status, 'R'); // one tick above
}

TEST(JudgeTradeTest, SellTriggerBoundary_ExactThresholdAcceptsOneTickBelowRejects) {
    EXPECT_EQ(judge(makeSnapshot(1'002'550, 1'002'650)).status, 'A'); // exactly 100.2550: >= holds
    EXPECT_EQ(judge(makeSnapshot(1'002'549, 1'002'649)).status, 'R'); // one tick below
}

TEST(JudgeTradeTest, LargeBand_IsNotTruncated) {
    // 5000 bps -> scaled 50'000'000 -> B = 0.5, so the buy threshold is 50.0000.
    // A narrow integer type would wrap this value and produce wrong verdicts.
    DecisionConfig cfg = makeConfig(50'000'000);
    EXPECT_EQ(judge(makeSnapshot(399'900, 400'000), 100, makeState(), cfg).status, 'A');
    EXPECT_EQ(judge(makeSnapshot(599'900, 600'000), 100, makeState(), cfg).status, 'R');
}

TEST(JudgeTradeTest, CrossedBookWithBothTriggers_PrefersBuy) {
    // Not possible in a sane book, but documents the tie-break
    NewOrder o = judge(makeSnapshot(1'003'000, 997'000));
    EXPECT_EQ(o.status, 'A');
    EXPECT_EQ(o.side, 'B');
    EXPECT_EQ(o.limit_px, 997'000);
}

TEST(JudgeTradeTest, MaxQuantityBoundary) {
    EXPECT_EQ(judge(BUY_QUOTE, MAX_QTY).status, 'A');     // == max is allowed
    EXPECT_EQ(judge(BUY_QUOTE, MAX_QTY + 1).status, 'R'); // > max is rejected
}

TEST(JudgeTradeTest, ParticipationBoundary) {
    // cap 15%: market 1000, already executed 100 -> may reach 150 total
    SessionState s = makeState(1000, 100);
    EXPECT_EQ(judge(BUY_QUOTE, 50, s).status, 'A'); // 150 == 15.0%: allowed
    EXPECT_EQ(judge(BUY_QUOTE, 51, s).status, 'R'); // 151 > 15.0%: rejected
}

TEST(JudgeTradeTest, ZeroMarketVolume_RejectsOnParticipation) {
    // Pins current behavior: with no volume observed, any positive qty exceeds the cap.
    // This is why the caller must count the trade's own volume before judging it.
    EXPECT_EQ(judge(BUY_QUOTE, 1, makeState(0, 0)).status, 'R');
}

TEST(JudgeTradeTest, Rejects_WhenVwapIsNotPositive) {
    EXPECT_EQ(judge(SELL_QUOTE, 100, makeState(), makeConfig(), 0).status, 'R');
}

// ============================ processTradeBytes ============================

TEST(ProcessTradeBytesTest, AcceptedTrade_EndToEnd) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);          // prior print at the same price: VWAP stays 100.0000
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum, encodeTrade(makeTrade(100, 7)), SYM,
                                    BUY_QUOTE, state, makeConfig(), WINDOW, rv);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 'A');
    EXPECT_EQ(result->side, 'B');
    EXPECT_EQ(result->limit_px, 997'000);
    EXPECT_EQ(result->trade_id, 7);
    EXPECT_EQ(result->qty, 100u);
    EXPECT_EQ(rv.getTickCount(), 2uz);
    EXPECT_EQ(state.market_volume, 1'000'100u);
    EXPECT_EQ(state.executed_qty, 100u);
    EXPECT_TRUE(accum.empty());
}

TEST(ProcessTradeBytesTest, RejectedTrade_CountsMarketVolumeButNotExecuted) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum, encodeTrade(makeTrade(100, 7)), SYM,
                                    NEUTRAL, state, makeConfig(), WINDOW, rv);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, 'R');
    EXPECT_EQ(state.executed_qty, 0u);
    EXPECT_EQ(state.market_volume, 1'000'100u);
}

TEST(ProcessTradeBytesTest, WarmUp_TooFewQuotes_NoOrderButVwapRecorded) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum, encodeTrade(makeTrade(100)), SYM,
                                    makeSnapshot(996'900, 997'000, /*quotes=*/9),
                                    state, makeConfig(), WINDOW, rv);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(rv.getTickCount(), 1uz);   // trade still feeds the VWAP window
}

TEST(ProcessTradeBytesTest, WarmUp_TooLittleTime_NoOrder) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum, encodeTrade(makeTrade(100)), SYM, BUY_QUOTE,
                                    state, makeConfig(), WINDOW - std::chrono::nanoseconds{1}, rv);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(rv.getTickCount(), 1uz);
}

TEST(ProcessTradeBytesTest, WarmUp_ExactBoundaryOpensGate) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    // exactly 10 quotes and exactly one full window: both conditions are inclusive
    auto result = processTradeBytes(accum, encodeTrade(makeTrade(100)), SYM,
                                    makeSnapshot(996'900, 997'000, 10),
                                    state, makeConfig(), WINDOW, rv);

    EXPECT_TRUE(result.has_value());
}

// Encodes the interpretation that trades during warm-up still count toward market
// volume (only the output is suppressed).
TEST(ProcessTradeBytesTest, WarmUp_TradesStillCountTowardMarketVolume) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    processTradeBytes(accum, encodeTrade(makeTrade(100)), SYM,
                      makeSnapshot(996'900, 997'000, 9), state, makeConfig(), WINDOW, rv);

    EXPECT_EQ(state.market_volume, 1'000'100u);
}

TEST(ProcessTradeBytesTest, OtherSymbol_IsIgnoredEntirely) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum,
                                    encodeTrade(makeTrade(100, 1, VWAP, 5'000'000, OTHER_SYM)),
                                    SYM, BUY_QUOTE, state, makeConfig(), WINDOW, rv);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(rv.getTickCount(), 0uz);
    EXPECT_EQ(state.market_volume, 1'000'000u);
    EXPECT_EQ(state.executed_qty, 0u);
}

TEST(ProcessTradeBytesTest, NonTradeMessage_IsIgnored) {
    // Regression guard for the get_if null-dereference class of bug
    Quote q{};
    q.symbol = SYM; q.ts_ns = 1; q.bid_qty = 1; q.bid_px = 1; q.ask_qty = 1; q.ask_px = 1;

    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto result = processTradeBytes(accum, encodeQuote(q), SYM, BUY_QUOTE,
                                    state, makeConfig(), WINDOW, rv);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(rv.getTickCount(), 0uz);
    EXPECT_EQ(state.market_volume, 1'000'000u);
}

TEST(ProcessTradeBytesTest, FragmentedTrade_ProducesOrderOnlyWhenComplete) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);
    SessionState state = makeState();
    std::vector<std::byte> accum;
    const auto bytes = encodeTrade(makeTrade(100, 9));

    auto first = processTradeBytes(accum, std::span<const std::byte>(bytes.data(), 10), SYM,
                                   BUY_QUOTE, state, makeConfig(), WINDOW, rv);
    EXPECT_FALSE(first.has_value());
    EXPECT_FALSE(accum.empty());          // partial bytes retained for the next call
    EXPECT_EQ(rv.getTickCount(), 1uz);    // nothing recorded yet

    auto second = processTradeBytes(accum, std::span<const std::byte>(bytes.data() + 10, bytes.size() - 10),
                                    SYM, BUY_QUOTE, state, makeConfig(), WINDOW, rv);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->trade_id, 9);
    EXPECT_TRUE(accum.empty());
    EXPECT_EQ(rv.getTickCount(), 2uz);
}

TEST(ProcessTradeBytesTest, TwoTradesInOneChunk_BothUpdateState) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto a = encodeTrade(makeTrade(100, 1, VWAP, 5'000'000));
    auto b = encodeTrade(makeTrade(100, 2, VWAP, 6'000'000));
    a.insert(a.end(), b.begin(), b.end());

    processTradeBytes(accum, a, SYM, NEUTRAL, state, makeConfig(), WINDOW, rv);

    EXPECT_EQ(rv.getTickCount(), 2uz);
    EXPECT_EQ(state.market_volume, 1'000'200u);
    EXPECT_TRUE(accum.empty());
}