#pragma once

#include <mutex>
#include <span>
#include <vector>

#include "RollingVwap.h"
#include "data/L1State.h"
#include "serialization/Decoder.h"

template<typename Handler>
void processBytes(std::vector<std::byte>& accumBuf, std::span<const std::byte> incomingBytes, Handler&& onMessage) {
    accumBuf.insert(accumBuf.end(), incomingBytes.begin(), incomingBytes.end());

    std::ptrdiff_t totalBytesConsumed{0z};
    while (true) {
        auto [message, bytesConsumed]
                {Decode(std::span<const std::byte>(accumBuf).subspan(totalBytesConsumed))};
        totalBytesConsumed += static_cast<std::ptrdiff_t>(bytesConsumed);
        if (bytesConsumed == 0)
            break;

        if (message)
            onMessage(message.value());
    }
    accumBuf.erase(accumBuf.begin(), accumBuf.begin() + totalBytesConsumed);
}

void processQuoteBytes(std::vector<std::byte> &accumBuf,
                       std::span<const std::byte> incomingBytes,
                       const std::array<char, 12> &symbol,
                       L1State &state);

void processTradeBytes(std::vector<std::byte> &accumBuf,
                       std::span<const std::byte> incomingBytes,
                       const std::array<char, 12> &symbol,
                       RollingVwap &rollingVwap);
