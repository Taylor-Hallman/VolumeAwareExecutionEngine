#pragma once

#include <cstdint>

// using a raw enum just so I don't have to static_cast it every time
enum MsgType : uint8_t {
    QUOTE = 1,
    TRADE = 2,
    HEARTBEAT = 3,
    SESSION_CONTROL = 4,
    NEW_ORDER = 10,
    EXEC_REPORT = 11
};
