#pragma once
#include "RollingVwap.h"
#include "data/L1State.h"
#include "data/GCMD_1/Trade.h"
#include "data/GCOE_1/NewOrder.h"

NewOrder judgeTrade(const L1State& state, const RollingVwap<VWAP_CAPACITY>& vwap, const Trade& trade, double bandBps, double participationCap);