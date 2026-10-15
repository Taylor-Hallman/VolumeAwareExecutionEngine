#pragma once
#include "RollingVwap.h"
#include "data/L1State.h"
#include "data/MarketInfo.h"
#include "data/GCMD_1/Trade.h"
#include "data/GCOE_1/NewOrder.h"

NewOrder judgeTrade(const MarketInfo& marketInfo, const Trade& trade);