#pragma once

#include <cstdint>

struct SessionControl {
    uint64_t ts_ns;
    uint8_t state; // 0=OPEN, 1=HALT, 2=CLOSE
};