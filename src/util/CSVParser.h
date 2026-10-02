#pragma once
#include <string_view>
#include <variant>
#include <vector>

#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"

uint64_t parseTimeToNsSinceOpen(std::string_view ts);
int64_t parseFixedPointPrice(const std::string& s);
std::vector<std::variant<Quote, Trade>> parseQuotesAndTrades(std::string_view inputFileName);