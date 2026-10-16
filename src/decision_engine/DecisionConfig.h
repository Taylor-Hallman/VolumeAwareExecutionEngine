#pragma once
#include <cstdint>

struct DecisionConfig {
    uint64_t band_bps_scaled;
    uint64_t participation_cap_scaled;
    uint32_t max_qty;
};
