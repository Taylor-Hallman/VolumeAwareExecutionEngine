#pragma once

#include <optional>
#include <span>
#include <variant>

#include "../data/GCMD_1/Heartbeat.h"
#include "../data/GCMD_1/Quote.h"
#include "../data/GCMD_1/SessionControl.h"
#include "../data/GCMD_1/Trade.h"
#include "../data/GCOE_1/ExecReport.h"
#include "../data/GCOE_1/NewOrder.h"

using Message = std::variant<Heartbeat, Quote, SessionControl, Trade, ExecReport, NewOrder>;

struct DecodeResult {
    std::optional<Message> message;
    size_t bytesConsumed{};
};

DecodeResult Decode(std::span<const std::byte> data);
