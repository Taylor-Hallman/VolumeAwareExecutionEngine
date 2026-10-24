#pragma once

#include <array>

struct NewOrder {
    static constexpr uint16_t SIZE{ 50u };
    static inline std::atomic<uint64_t> s_next_id{ 0 };
    uint64_t client_order_id{ s_next_id.fetch_add(1, std::memory_order_relaxed) };
    std::array<char, 12> symbol;
    char status; // 'A' for accepted, 'R' for rejected
    uint64_t ts_ns;
    int64_t trade_id;
    char side; // 'B' or 'S'
    uint32_t qty;
    int64_t limit_px;
};