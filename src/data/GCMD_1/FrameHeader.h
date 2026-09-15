#pragma once

#include <cstdint>

struct FrameHeader {
    uint16_t body_len;
    uint8_t msg_type;
    uint8_t version;
};
