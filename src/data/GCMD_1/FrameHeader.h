#pragma once

#include <cstdint>

struct FrameHeader {
    static constexpr uint16_t SIZE{ 4u };
    uint16_t body_len;
    uint8_t msg_type;
    uint8_t version;
};
