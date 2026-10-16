#pragma once

#include <cstdint>

struct L1Snapshot {
    int quotesObserved;
    int64_t best_bid_px;
    int64_t best_ask_px;
};