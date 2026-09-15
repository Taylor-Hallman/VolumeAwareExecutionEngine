#pragma once
#include <variant>
#include <vector>

#include "data/GCMD_1/Quote.h"
#include "data/Trade.h"

std::vector<std::variant<Quote, Trade>> parseQuotesAndTrades();
