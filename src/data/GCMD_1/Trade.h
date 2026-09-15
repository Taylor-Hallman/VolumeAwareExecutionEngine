#pragma once

#include <array>
#include <cstdint>

struct Trade {
    static inline int64_t s_next_id{};
    std::array<char, 12> symbol;
    uint64_t ts_ns;
    uint32_t qty;
    int64_t px;
    char aggressor; // 'B', 'S', or '?' if unknown
    int64_t id{ s_next_id++ };
};