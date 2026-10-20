#pragma once

#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"

template<typename Callback>
void replayEvents(const std::vector<std::variant<Quote, Trade>>& events, std::atomic<bool>& cancelFlag, Callback&& onEvent) {
    auto programStart{ std::chrono::steady_clock::now() };
    for (const auto& event : events) {
        if (cancelFlag.load(std::memory_order_relaxed))
            break;
        uint64_t ts_ns = std::visit([](const auto& e) {
            return e.ts_ns;
        }, event);
        std::this_thread::sleep_until(programStart + std::chrono::nanoseconds(ts_ns));
        std::visit(onEvent, event);
    }
}
