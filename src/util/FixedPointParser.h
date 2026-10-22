#pragma once

#include <cstdint>
#include <string>

uint64_t parseFixedPoint(std::string_view s);
int getFixedPointPrecision(std::string_view s);