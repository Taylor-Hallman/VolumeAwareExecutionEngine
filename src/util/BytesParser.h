#pragma once

#include <mutex>
#include <print>
#include <span>
#include <vector>

#include "../decision_engine/DecisionEngine.h"
#include "RollingVwap.h"
#include "data/L1State.h"
#include "serialization/Decoder.h"

template<typename Handler>
void processBytes(std::vector<std::byte> &accumBuf, std::span<const std::byte> incomingBytes, Handler &&onMessage) {
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

template<std::size_t VwapCapacity, typename Callback>
void processTradeBytes(std::vector<std::byte> &accumBuf,
                                          std::span<const std::byte> incomingBytes,
                                          const std::array<char, 12> &symbol,
                                          const L1Snapshot& snapshot,
                                          SessionState& state,
                                          const DecisionConfig& config,
                                          const std::chrono::nanoseconds timeElapsed,
                                          RollingVwap<VwapCapacity> &rollingVwap, Callback&& onOrderReceived) {
    processBytes(accumBuf, incomingBytes, [&](const Message &msg) {
        if (const auto *t{std::get_if<Trade>(&msg)}) {
            if (symbol == t->symbol) {
                rollingVwap.addTick(t->ts_ns, t->px, t->qty);
                state.market_volume += t->qty;
                state.market_notional += t->px * t->qty;

                const std::chrono::nanoseconds window{ rollingVwap.getWindowNs() };
                if (snapshot.quotesObserved < 10 || timeElapsed < window)
                    return;

                NewOrder newOrder = judgeTrade(*t, snapshot, state, config, rollingVwap.getVwap());
                onOrderReceived(newOrder);
                if (newOrder.status == 'A') {
                    state.executed_qty += t->qty;
                    state.executed_notional += t->px * t->qty;
                    if (newOrder.side == 'B') {
                        state.buy_qty += t->qty;
                        state.buy_notional += t->px * t->qty;
                    }
                    else {
                        state.sell_qty += t->qty;
                        state.sell_notional += t->px * t->qty;
                    }
                }
            }
        }
        else {
            std::println("[WARN] Expected Trade, but received malformed input");
        }
    });
}
