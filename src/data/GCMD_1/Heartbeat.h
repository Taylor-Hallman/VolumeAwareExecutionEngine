#pragma once

#include <cstdint>

struct Heartbeat {
    static constexpr uint16_t SIZE{ 8u };
    uint64_t ts_ns;
};
