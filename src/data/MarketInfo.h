#pragma once
#include <cstdint>

// TODO split this struct up
struct MarketInfo {
    const uint64_t band_bps_scaled;
    const uint64_t participation_cap_scaled;
    const uint32_t max_qty;

    int64_t best_bid_px{}, best_ask_px{};
    int64_t vwap{};
    uint64_t market_volume{};
    uint64_t executed_qty{};
    bool has_passed_warmup_gate{ false };

    MarketInfo(uint64_t bandBps, uint64_t participationCap, uint32_t maxQty)
        : band_bps_scaled(bandBps), participation_cap_scaled(participationCap), max_qty(maxQty) {}
};