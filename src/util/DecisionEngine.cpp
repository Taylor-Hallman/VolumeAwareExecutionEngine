#include "DecisionEngine.h"

NewOrder judgeTrade(const MarketInfo& marketInfo, const Trade& trade) {
    const NewOrder rejected{
        .symbol = trade.symbol,
        .status = 'R',
        .ts_ns = trade.ts_ns,
        .trade_id = trade.id,
        .qty = trade.qty
    };

    // reject if quantity exceeds the max quantity, or if warm up gate has not passed
    if (trade.qty > marketInfo.max_qty || !marketInfo.has_passed_warmup_gate)
        return rejected;

    // reject if trade would break through the participation cap
    // executed_qty + trade_qty > cap * market_volume; cap = capScaled / 10,000
    // executed_qty + trade_qty > capScaled / 10,000 * market_volume
    // 10,000 * (executed_qty + trade_qty) > capScaled * market_volume
    if (10'000 * (marketInfo.executed_qty + trade.qty) > marketInfo.participation_cap_scaled * marketInfo.market_volume)
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
    const auto vwap{ marketInfo.vwap };
    const auto bandBpsScaled{ marketInfo.band_bps_scaled };
    const bool buyTrigger{ marketInfo.best_ask_px * BAND_SCALE <= vwap * (BAND_SCALE - static_cast<__int128>(bandBpsScaled)) };
    const bool sellTrigger{ marketInfo.best_bid_px * BAND_SCALE >= vwap * (BAND_SCALE + static_cast<__int128>(bandBpsScaled)) };

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
        .limit_px = buyTrigger ? marketInfo.best_ask_px : marketInfo.best_bid_px
    };
}