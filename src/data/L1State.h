#pragma once

#include <cstdint>

struct L1State {
    static inline int quotesObserved{};
    int64_t bid_px{};
    uint32_t bid_qty{};
    int64_t ask_px{};
    uint32_t ask_qty{};
};