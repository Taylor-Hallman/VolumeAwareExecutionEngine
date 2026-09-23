#pragma once

#include <span>
#include <vector>

#include "../data/GCMD_1/Heartbeat.h"
#include "../data/GCMD_1/Quote.h"
#include "../data/GCMD_1/SessionControl.h"
#include "../data/GCMD_1/Trade.h"
#include "../data/GCOE_1/ExecReport.h"
#include "../data/GCOE_1/NewOrder.h"

size_t Encode(const Heartbeat& hb, std::span<std::byte> dest);
size_t Encode(const Quote& quote, std::span<std::byte> dest);
size_t Encode(const SessionControl& sc, std::span<std::byte> dest);
size_t Encode(const Trade& trade, std::span<std::byte> dest);
size_t Encode(const ExecReport& report, std::span<std::byte> dest);
size_t Encode(const NewOrder& newOrder, std::span<std::byte> dest);