#pragma once

#include <vector>

#include "../data/GCMD_1/Heartbeat.h"
#include "../data/GCMD_1/Quote.h"
#include "../data/GCMD_1/SessionControl.h"
#include "../data/GCMD_1/Trade.h"
#include "../data/GCOE_1/ExecReport.h"
#include "../data/GCOE_1/NewOrder.h"

std::vector<std::byte> Encode(const Heartbeat& hb);
std::vector<std::byte> Encode(const Quote& quote);
std::vector<std::byte> Encode(const SessionControl& sc);
std::vector<std::byte> Encode(const Trade& trade);
std::vector<std::byte> Encode(const ExecReport& report);
std::vector<std::byte> Encode(const NewOrder& newOrder);