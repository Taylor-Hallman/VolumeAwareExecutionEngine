#pragma once

#include <cstdint>

struct ExecReport {
    uint64_t client_order_id;
    uint64_t ts_ns;
    uint8_t status; // 0=ACK, 1=FILL, 2=PARTIAL, 3=REJECT
    uint32_t filled_qty;
    int64_t avg_px;
    uint8_t reason_code; // 0=none, 1=risk, 2=price, 3=size, 4=throttle
};