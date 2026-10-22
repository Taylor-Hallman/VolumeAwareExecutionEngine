#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
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
constexpr std::array<char, 12> SYM{'S','Y','N','T','H','1'};
constexpr std::array<char, 12> OTHER_SYM{'S','Y','N','T','H','2'};

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

namespace {

// Notional accumulators may be __int128; narrow for assertions (values here are small).
int64_t i64(__int128 v) { return static_cast<int64_t>(v); }

// Feeds one chunk through processTradeBytes and returns every order delivered to the callback.
template <std::size_t Cap>
std::vector<NewOrder> run(std::vector<std::byte>& accum,
                          std::span<const std::byte> bytes,
                          const L1Snapshot& snapshot,
                          SessionState& state,
                          RollingVwap<Cap>& rv,
                          std::chrono::nanoseconds elapsed = WINDOW) {
    std::vector<NewOrder> orders;
    processTradeBytes(accum, bytes, SYM, snapshot, state, makeConfig(), elapsed, rv,
                      [&orders](const NewOrder& o) { orders.push_back(o); });
    return orders;
}

constexpr int64_t TRADE_NOTIONAL = VWAP * 100;   // px * qty for makeTrade(100) at VWAP

} // namespace

TEST(ProcessTradeBytesTest, AcceptedBuy_EndToEnd) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);   // prior print at VWAP keeps the VWAP at 100.0000
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100, 7)), BUY_QUOTE, state, rv);

    ASSERT_EQ(orders.size(), 1uz);
    EXPECT_EQ(orders[0].status, 'A');
    EXPECT_EQ(orders[0].side, 'B');
    EXPECT_EQ(orders[0].limit_px, 997'000);
    EXPECT_EQ(orders[0].trade_id, 7);
    EXPECT_EQ(orders[0].qty, 100u);

    EXPECT_EQ(rv.getTickCount(), 2uz);
    EXPECT_EQ(state.market_volume, 1'000'100u);
    EXPECT_EQ(i64(state.market_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.executed_qty, 100u);
    // Notional uses the TRADE's price (1'000'000), not the order's limit_px (997'000)
    EXPECT_EQ(i64(state.executed_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.buy_qty, 100u);
    EXPECT_EQ(i64(state.buy_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.sell_qty, 0u);
    EXPECT_EQ(i64(state.sell_notional), 0);
    EXPECT_TRUE(accum.empty());
}

TEST(ProcessTradeBytesTest, AcceptedSell_UpdatesSellAccumulatorsOnly) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100, 8)), SELL_QUOTE, state, rv);

    ASSERT_EQ(orders.size(), 1uz);
    EXPECT_EQ(orders[0].status, 'A');
    EXPECT_EQ(orders[0].side, 'S');
    EXPECT_EQ(orders[0].limit_px, 1'003'000);

    EXPECT_EQ(state.executed_qty, 100u);
    EXPECT_EQ(state.sell_qty, 100u);
    EXPECT_EQ(i64(state.sell_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.buy_qty, 0u);
    EXPECT_EQ(i64(state.buy_notional), 0);
}

TEST(ProcessTradeBytesTest, RejectedTrade_CountsMarketTotalsButNotExecuted) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100, 7)), NEUTRAL, state, rv);

    ASSERT_EQ(orders.size(), 1uz);       // a rejection is still delivered
    EXPECT_EQ(orders[0].status, 'R');
    EXPECT_EQ(orders[0].trade_id, 7);

    EXPECT_EQ(state.market_volume, 1'000'100u);
    EXPECT_EQ(i64(state.market_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.executed_qty, 0u);
    EXPECT_EQ(i64(state.executed_notional), 0);
    EXPECT_EQ(state.buy_qty, 0u);
    EXPECT_EQ(state.sell_qty, 0u);
}

TEST(ProcessTradeBytesTest, TradeCountsTowardMarketVolumeBeforeItIsJudged) {
    // Pins the "count first, then judge" order. With no prior volume, the trade itself is
    // the only market volume (100), and accepting 100 against it is 100% participation,
    // far above the 15% cap, so it must be rejected.
    RollingVwap<8> rv(10);
    SessionState state = makeState(/*marketVolume=*/0, /*executed=*/0);
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100)), BUY_QUOTE, state, rv);

    ASSERT_EQ(orders.size(), 1uz);
    EXPECT_EQ(orders[0].status, 'R');
    EXPECT_EQ(state.market_volume, 100u);
}

// ---------------------------- warm-up gate ----------------------------

TEST(ProcessTradeBytesTest, WarmUp_TooFewQuotes_NoOrderButStateStillUpdates) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100)),
                      makeSnapshot(996'900, 997'000, /*quotes=*/9), state, rv);

    EXPECT_TRUE(orders.empty());
    EXPECT_EQ(rv.getTickCount(), 1uz);                    // still feeds the VWAP window
    EXPECT_EQ(state.market_volume, 1'000'100u);           // still counts as market volume
    EXPECT_EQ(i64(state.market_notional), TRADE_NOTIONAL);
    EXPECT_EQ(state.executed_qty, 0u);
}

TEST(ProcessTradeBytesTest, WarmUp_TooLittleTime_NoOrder) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100)), BUY_QUOTE, state, rv,
                      WINDOW - std::chrono::nanoseconds{1});

    EXPECT_TRUE(orders.empty());
    EXPECT_EQ(rv.getTickCount(), 1uz);
    EXPECT_EQ(state.market_volume, 1'000'100u);
}

TEST(ProcessTradeBytesTest, WarmUp_ExactBoundaryOpensGate) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    // exactly 10 quotes and exactly one full window: both conditions are inclusive
    auto orders = run(accum, encodeTrade(makeTrade(100)),
                      makeSnapshot(996'900, 997'000, 10), state, rv, WINDOW);

    EXPECT_EQ(orders.size(), 1uz);
}

// ---------------------------- filtering ----------------------------

TEST(ProcessTradeBytesTest, OtherSymbol_IsIgnoredEntirely) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeTrade(makeTrade(100, 1, VWAP, 5'000'000, OTHER_SYM)),
                      BUY_QUOTE, state, rv);

    EXPECT_TRUE(orders.empty());
    EXPECT_EQ(rv.getTickCount(), 0uz);
    EXPECT_EQ(state.market_volume, 1'000'000u);
    EXPECT_EQ(i64(state.market_notional), 0);
    EXPECT_EQ(state.executed_qty, 0u);
}

TEST(ProcessTradeBytesTest, NonTradeMessage_IsIgnored) {
    // Regression guard for the get_if null-dereference class of bug.
    // (Prints a "[WARN] Expected Trade" line to stdout; that's expected.)
    Quote q{};
    q.symbol = SYM; q.ts_ns = 1; q.bid_qty = 1; q.bid_px = 1; q.ask_qty = 1; q.ask_px = 1;

    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto orders = run(accum, encodeQuote(q), BUY_QUOTE, state, rv);

    EXPECT_TRUE(orders.empty());
    EXPECT_EQ(rv.getTickCount(), 0uz);
    EXPECT_EQ(state.market_volume, 1'000'000u);
    EXPECT_TRUE(accum.empty());   // the message was consumed, not left behind
}

// ---------------------------- stream reassembly ----------------------------

TEST(ProcessTradeBytesTest, FragmentedTrade_ProducesOrderOnlyWhenComplete) {
    RollingVwap<8> rv(10);
    rv.addTick(1'000'000, VWAP, 100);
    SessionState state = makeState();
    std::vector<std::byte> accum;
    const auto bytes = encodeTrade(makeTrade(100, 9));

    auto first = run(accum, std::span<const std::byte>(bytes.data(), 10), BUY_QUOTE, state, rv);
    EXPECT_TRUE(first.empty());
    EXPECT_FALSE(accum.empty());                 // partial bytes retained for the next call
    EXPECT_EQ(rv.getTickCount(), 1uz);           // nothing recorded yet
    EXPECT_EQ(state.market_volume, 1'000'000u);

    auto second = run(accum, std::span<const std::byte>(bytes.data() + 10, bytes.size() - 10),
                      BUY_QUOTE, state, rv);
    ASSERT_EQ(second.size(), 1uz);
    EXPECT_EQ(second[0].trade_id, 9);
    EXPECT_TRUE(accum.empty());
    EXPECT_EQ(rv.getTickCount(), 2uz);
    EXPECT_EQ(state.market_volume, 1'000'100u);
}

TEST(ProcessTradeBytesTest, TwoTradesInOneChunk_BothVerdictsDeliveredInOrder) {
    // The case the callback design exists for: the old single-return version lost the
    // first verdict. Trade 1 (qty 100) is accepted; trade 2 (qty over the max) is rejected.
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto chunk = encodeTrade(makeTrade(100, 1, VWAP, 5'000'000));
    auto second = encodeTrade(makeTrade(MAX_QTY + 1, 2, VWAP, 6'000'000));
    chunk.insert(chunk.end(), second.begin(), second.end());

    auto orders = run(accum, chunk, BUY_QUOTE, state, rv);

    ASSERT_EQ(orders.size(), 2uz);
    EXPECT_EQ(orders[0].trade_id, 1);
    EXPECT_EQ(orders[0].status, 'A');
    EXPECT_EQ(orders[1].trade_id, 2);
    EXPECT_EQ(orders[1].status, 'R');

    EXPECT_EQ(rv.getTickCount(), 2uz);
    EXPECT_EQ(state.market_volume, 1'000'000u + 100 + (MAX_QTY + 1));
    EXPECT_EQ(state.executed_qty, 100u);          // only the accepted trade
    EXPECT_EQ(state.buy_qty, 100u);
    EXPECT_TRUE(accum.empty());
}

TEST(ProcessTradeBytesTest, TwoAcceptedTradesInOneChunk_AccumulateExecutedTotals) {
    RollingVwap<8> rv(10);
    SessionState state = makeState();
    std::vector<std::byte> accum;

    auto chunk = encodeTrade(makeTrade(100, 1, VWAP, 5'000'000));
    auto second = encodeTrade(makeTrade(100, 2, VWAP, 6'000'000));
    chunk.insert(chunk.end(), second.begin(), second.end());

    auto orders = run(accum, chunk, BUY_QUOTE, state, rv);

    ASSERT_EQ(orders.size(), 2uz);
    EXPECT_EQ(orders[0].status, 'A');
    EXPECT_EQ(orders[1].status, 'A');
    EXPECT_EQ(state.executed_qty, 200u);
    EXPECT_EQ(i64(state.executed_notional), 2 * TRADE_NOTIONAL);
    EXPECT_EQ(state.buy_qty, 200u);
}