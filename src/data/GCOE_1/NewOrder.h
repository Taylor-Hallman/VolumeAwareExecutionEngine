#pragma once

#include <array>

struct NewOrder {
    uint64_t client_order_id;
    std::array<char, 12> symbol;
    char status; // 'A' for accepted, 'R' for rejected
    uint64_t ts_ns;
    int64_t trade_id;
    char side; // 'B' or 'S'
    uint32_t qty;
    int64_t limit_px;
};