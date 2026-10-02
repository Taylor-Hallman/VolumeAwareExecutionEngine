#pragma once

#include <mutex>
#include <span>
#include <vector>

#include "data/L1State.h"

void processQuoteBytes(std::vector<std::byte>& accumBuf,
                    std::span<const std::byte> incomingBytes,
                    const std::array<char, 12>& symbol,
                    L1State& state,
                    std::mutex& stateMutex);