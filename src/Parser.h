#pragma once
#include <variant>
#include <vector>

#include "data/GCMD_1/Quote.h"
#include "data/GCMD_1/Trade.h"

std::vector<std::variant<Quote, Trade>> parseQuotesAndTrades();
