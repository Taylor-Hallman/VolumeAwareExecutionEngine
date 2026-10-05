#pragma once

#include <cstdint>
#include <map>
#include <mutex>

struct L1State {
    int quotesObserved{};
    int64_t bid_px{};
    uint32_t bid_qty{};
    int64_t ask_px{};
    uint32_t ask_qty{};

    std::mutex stateMutex;
};
