#include <random>
#include <gtest/gtest.h>

#include "util/RollingVwap.h"

static constexpr auto NS_PER_MS{ 1'000'000 };

TEST(RollingVwapTest, NoTicks) {
    RollingVwap<4> rollingVwap(10);

    EXPECT_EQ(rollingVwap.getTickCount(), 0uz);
    EXPECT_EQ(rollingVwap.getVwap(), 0);
}

TEST(RollingVwapTest, SingleTick) {
    RollingVwap<4> rollingVwap(10);
    rollingVwap.addTick(1, 10000, 10);

    EXPECT_EQ(rollingVwap.getTickCount(), 1uz);
    EXPECT_EQ(rollingVwap.getVwap(), 10000);
}

TEST(RollingVwapTest, MultipleTicksSameWindow) {
    RollingVwap<4> rollingVwap(10);

    rollingVwap.addTick(1, 10000, 10);
    rollingVwap.addTick(2, 11000, 20);

    EXPECT_EQ(rollingVwap.getTickCount(), 2uz);
    EXPECT_EQ(rollingVwap.getVwap(), 10667); // (10000*10 + 11000*20) / (10 + 20) ~= 10667
}

TEST(RollingVwapTest, SlidingWindowExcludesOldTicks) {
    RollingVwap<4> rollingVwap(10);

    rollingVwap.addTick(4 * NS_PER_MS, 90000, 10);
    rollingVwap.addTick(6 * NS_PER_MS, 10000, 10);
    rollingVwap.addTick(15 * NS_PER_MS, 11000, 20);

    EXPECT_EQ(rollingVwap.getTickCount(), 2uz);
    EXPECT_EQ(rollingVwap.getVwap(), 10667);
}

TEST(RollingVwapTest, ExactWindowBoundary) {
    RollingVwap<4> rollingVwap(10);

    rollingVwap.addTick(0, 10000, 10);
    rollingVwap.addTick(10 * NS_PER_MS, 11000, 20);

    EXPECT_EQ(rollingVwap.getTickCount(), 2uz);
    EXPECT_EQ(rollingVwap.getVwap(), 10667);
}

// --- Rounding policy (round-half-up, integer arithmetic only) ---

TEST(RollingVwapTest, Rounding_ExactHalfRoundsUp) {
    RollingVwap<4> v(1000);
    v.addTick(1, 10001, 1);
    v.addTick(2, 10002, 1);
    // 20003 / 2 = 10001.5 -> 10002
    EXPECT_EQ(v.getVwap(), 10002);
}

TEST(RollingVwapTest, Rounding_BelowHalfRoundsDown) {
    RollingVwap<4> v(1000);
    v.addTick(1, 10000, 2);
    v.addTick(2, 10001, 1);
    // 30001 / 3 = 10000.33 -> 10000
    EXPECT_EQ(v.getVwap(), 10000);
}

TEST(RollingVwapTest, Rounding_AboveHalfRoundsUp) {
    RollingVwap<4> v(1000);
    v.addTick(1, 10000, 1);
    v.addTick(2, 10001, 2);
    // 30002 / 3 = 10000.67 -> 10001
    EXPECT_EQ(v.getVwap(), 10001);
}

// --- Window boundary policy ---

TEST(RollingVwapTest, OneNanosecondPastWindow_IsEvicted) {
    RollingVwap<4> v(10);
    v.addTick(0, 10000, 10);
    // Exactly windowNs old is kept (see ExactWindowBoundary); one ns more is evicted.
    v.addTick(10 * NS_PER_MS + 1, 11000, 20);
    EXPECT_EQ(v.getTickCount(), 1uz);
    EXPECT_EQ(v.getVwap(), 11000);
}

TEST(RollingVwapTest, FarFutureTickEvictsEverythingElse) {
    RollingVwap<4> v(10);
    v.addTick(0, 10000, 10);
    v.addTick(1 * NS_PER_MS, 20000, 10);
    v.addTick(2 * NS_PER_MS, 30000, 10);
    v.addTick(1000 * NS_PER_MS, 50000, 7);

    EXPECT_EQ(v.getTickCount(), 1uz);
    // Exact equality only holds if every evicted tick's contribution
    // was subtracted from both running sums with no residue.
    EXPECT_EQ(v.getVwap(), 50000);
}

// --- Capacity overflow policy: oldest tick overwritten even if still in window ---

TEST(RollingVwapTest, CapacityOverflow_KeepsOnlyMostRecentCapacityTicks) {
    RollingVwap<4> v(1000);  // large window: nothing evicts by time
    for (int i = 1; i <= 6; ++i)
        v.addTick(i, i * 10000, 10);

    EXPECT_EQ(v.getTickCount(), 4uz);
    // Live ticks are 30000, 40000, 50000, 60000 -> 45000.
    // If overwritten ticks stayed in the running sums, this would differ.
    EXPECT_EQ(v.getVwap(), 45000);
}

TEST(RollingVwapTest, CapacityOverflow_RepeatedWrapsStayConsistent) {
    RollingVwap<4> v(1000);
    for (int i = 1; i <= 100; ++i)
        v.addTick(i, i * 100, 10);

    EXPECT_EQ(v.getTickCount(), 4uz);
    // Last four: 9700, 9800, 9900, 10000 -> 9850
    EXPECT_EQ(v.getVwap(), 9850);
}

// --- Index wraparound combined with time-based eviction ---

TEST(RollingVwapTest, WraparoundWithTimeEviction) {
    RollingVwap<4> v(10);
    v.addTick(0 * NS_PER_MS, 10000, 10);
    v.addTick(1 * NS_PER_MS, 20000, 10);
    v.addTick(2 * NS_PER_MS, 30000, 10);
    v.addTick(3 * NS_PER_MS, 40000, 10);
    EXPECT_EQ(v.getTickCount(), 4uz);
    EXPECT_EQ(v.getVwap(), 25000);

    // Evicts the 0ms tick (age 11 > 10); the 1ms tick (age exactly 10) survives.
    v.addTick(11 * NS_PER_MS, 50000, 10);
    EXPECT_EQ(v.getTickCount(), 4uz);
    EXPECT_EQ(v.getVwap(), 35000);  // 20000..50000

    // head/tail have now wrapped past the end of the 4-slot array.
    v.addTick(12 * NS_PER_MS, 60000, 10);
    EXPECT_EQ(v.getTickCount(), 4uz);
    EXPECT_EQ(v.getVwap(), 45000);  // 30000..60000

    v.addTick(13 * NS_PER_MS, 70000, 10);
    EXPECT_EQ(v.getTickCount(), 4uz);
    EXPECT_EQ(v.getVwap(), 55000);  // 40000..70000
}

// --- Large-value safety: proves products are widened to __int128 before multiplying ---

TEST(RollingVwapTest, LargeValues_DoNotOverflowAccumulator) {
    RollingVwap<4> v(1000);
    constexpr int64_t px = std::numeric_limits<int64_t>::max();
    constexpr uint32_t qty = std::numeric_limits<uint32_t>::max();
    v.addTick(1, px, qty);
    v.addTick(2, px, qty);
    // Each product is ~3.96e28, far beyond 64 bits.
    EXPECT_EQ(v.getVwap(), px);
}

TEST(RollingVwapTest, LargeValues_MixedPricesAverageCorrectly) {
    RollingVwap<4> v(1000);
    constexpr uint32_t qty = std::numeric_limits<uint32_t>::max();
    v.addTick(1, 8'000'000'000'000'000'000LL, qty);
    v.addTick(2, 4'000'000'000'000'000'000LL, qty);
    EXPECT_EQ(v.getVwap(), 6'000'000'000'000'000'000LL);
}

// --- Churn test: compare against a brute-force recomputation ---

namespace {
template <size_t Capacity>
void runChurn(uint64_t windowMs, uint64_t seed, int numTicks) {
    RollingVwap<Capacity> vwap(windowMs);
    const uint64_t windowNs = windowMs * NS_PER_MS;

    std::mt19937_64 rng(seed);  // fixed seed: reproducible
    std::uniform_int_distribution<int64_t> pxDist(900'000, 1'100'000);
    std::uniform_int_distribution<uint32_t> qtyDist(1, 500);
    std::uniform_int_distribution<uint64_t> gapDist(0, 3 * NS_PER_MS);

    struct Ref { uint64_t ts; int64_t px; uint32_t qty; };
    std::vector<Ref> all;
    all.reserve(numTicks);
    uint64_t ts = 0;

    for (int i = 0; i < numTicks; ++i) {
        ts += gapDist(rng);
        Ref t{ ts, pxDist(rng), qtyDist(rng) };
        all.push_back(t);
        vwap.addTick(t.ts, t.px, t.qty);

        // Brute force: newest-first, stop at the window edge or at Capacity entries.
        __int128 num = 0;
        uint64_t den = 0;
        size_t cnt = 0;
        for (auto it = all.rbegin(); it != all.rend() && cnt < Capacity; ++it) {
            if (ts - it->ts > windowNs)
                break;
            num += static_cast<__int128>(it->px) * it->qty;
            den += it->qty;
            ++cnt;
        }
        const auto expected = static_cast<int64_t>((num + den / 2) / den);

        ASSERT_EQ(vwap.getTickCount(), cnt) << "mismatch at tick " << i;
        ASSERT_EQ(vwap.getVwap(), expected) << "mismatch at tick " << i;
    }
}
}  // namespace

TEST(RollingVwapTest, Churn_TimeWindowDominates) {
    // ~33 live ticks on average, capacity never binds
    ASSERT_NO_FATAL_FAILURE(runChurn<4096>(50, 12345, 5000));
}

TEST(RollingVwapTest, Churn_CapacityDominates) {
    // ~33 ticks would be live by time, but only 8 fit: constant overflow
    ASSERT_NO_FATAL_FAILURE(runChurn<8>(50, 6789, 5000));
}

TEST(RollingVwapTest, Churn_TinyWindow) {
    // 1 ms window: almost every add evicts several ticks
    ASSERT_NO_FATAL_FAILURE(runChurn<8>(1, 424242, 5000));
}