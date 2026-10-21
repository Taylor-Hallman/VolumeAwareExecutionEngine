#pragma once

#include <cstdint>

struct SessionState {
      uint64_t market_volume{};
      uint64_t executed_qty{};
      __int128 market_notional{};
      __int128 executed_notional{};
      __int128 buy_notional{}, buy_qty{};
      __int128 sell_notional{}, sell_qty{};
};