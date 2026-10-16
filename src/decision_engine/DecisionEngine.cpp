#include "decision_engine/DecisionEngine.h"

NewOrder judgeTrade(const Trade& trade, const L1Snapshot& snapshot, const SessionState& state,
                    const DecisionConfig& config, const int64_t vwap) {
    const NewOrder rejected{
        .symbol = trade.symbol,
        .status = 'R',
        .ts_ns = trade.ts_ns,
        .trade_id = trade.id,
        .qty = trade.qty
    };

    // reject if quantity exceeds the max quantity
    if (trade.qty > config.max_qty)
        return rejected;

    // reject if trade would break through the participation cap
    // executed_qty + trade_qty > cap * market_volume; cap = capScaled / 10,000
    // executed_qty + trade_qty > capScaled / 10,000 * market_volume
    // 10,000 * (executed_qty + trade_qty) > capScaled * market_volume
    if (10'000 * (state.executed_qty + trade.qty) > config.participation_cap_scaled * state.market_volume)
        return rejected;

    // best_ask_px <= vwap * (1 - B), where B = bandBps / 10,000
    // bandBps = bandBpsScaled / 10,000
    // therefore:
    // B = bandBpsScaled / 10,000 / 10,000 = bandBpsScaled / 10^8
    // 1 - B = 1 - bandBpsScaled / 10^8 = (10^8 - bandBpsScaled) / 10^8
    // Substitute:
    // best_ask_px <= vwap * (10^8 - bandBpsScaled) / 10^8
    // best_ask_px * 10^8 <= vwap * (10^8 - bandBpsScaled)
    // by similar logic, best_bid_px >= vwap * (1 + B) <==> best_bid_px * 10^8 >= vwap * (10^8 + bandBpsScaled)
    constexpr __int128 BAND_SCALE{ 100'000'000 };
    const auto bandBpsScaled{ config.band_bps_scaled };
    const bool buyTrigger{ snapshot.best_ask_px * BAND_SCALE
                            <= vwap * (BAND_SCALE - static_cast<__int128>(bandBpsScaled)) };
    const bool sellTrigger{ snapshot.best_bid_px * BAND_SCALE
                            >= vwap * (BAND_SCALE + static_cast<__int128>(bandBpsScaled)) };

    // reject if trade has not met either trigger condition
    if (!buyTrigger && !sellTrigger)
        return rejected;

    return NewOrder{
        .symbol = trade.symbol,
        .status = 'A',
        .ts_ns = trade.ts_ns,
        .trade_id = trade.id,
        .side = buyTrigger ? 'B' : 'S',
        .qty = trade.qty,
        .limit_px = buyTrigger ? snapshot.best_ask_px : snapshot.best_bid_px
    };
}