#include "FixedPointParser.h"

#include <charconv>
#include <stdexcept>

uint64_t parseFixedPoint(const std::string& s) {
    if (s.empty())
        throw std::invalid_argument("string was empty");

    if (s[0] == '-')
        throw std::invalid_argument("expected string representing an unsigned integer");

    auto dot = s.find('.');
    std::string intPart = dot == std::string::npos ? s : s.substr(0, dot);
    std::string fracPart = dot == std::string::npos ? "" : s.substr(dot + 1);

    if (intPart.empty())
        intPart = "0";

    fracPart.resize(4, '0'); // pad or truncate to exactly 4 digits

    uint64_t whole, frac;
    auto [ptr1, ec1] = std::from_chars(intPart.c_str(), intPart.c_str() + intPart.size(), whole);
    auto [ptr2, ec2] = std::from_chars(fracPart.c_str(), fracPart.c_str() + fracPart.size(), frac);
    if (ptr1 != intPart.c_str() + intPart.size() || ptr2 != fracPart.c_str() + fracPart.size())
        throw std::invalid_argument("string was malformed");

    return whole * 10000 + frac;
}

int getFixedPointPrecision(const std::string& s) {
    auto dot = s.find('.');
    if (dot == std::string::npos)
        return 0;
    return static_cast<int>(s.size() - 1 - dot);
}
