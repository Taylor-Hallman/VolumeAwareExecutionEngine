#pragma once

#include "DecisionConfig.h"
#include "L1Snapshot.h"
#include "SessionState.h"
#include "data/GCMD_1/Trade.h"
#include "data/GCOE_1/NewOrder.h"

NewOrder judgeTrade(const Trade& trade, const L1Snapshot& snapshot, const SessionState& state,
                    const DecisionConfig& config, int64_t vwap);