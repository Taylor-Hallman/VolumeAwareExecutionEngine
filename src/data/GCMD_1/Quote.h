#pragma once

#include <array>
#include <cstdint>

struct Quote {
    static constexpr uint16_t SIZE{ 44u };
    std::array<char, 12> symbol;
    uint64_t ts_ns;
    uint32_t bid_qty;
    int64_t bid_px;
    uint32_t ask_qty;
    int64_t ask_px;
};
