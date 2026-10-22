#include "FixedPointParser.h"

#include <charconv>
#include <stdexcept>

uint64_t parseFixedPoint(const std::string_view s) {
    if (s.empty())
        throw std::invalid_argument("string was empty");

    if (s[0] == '-')
        throw std::invalid_argument("expected string representing an unsigned integer");

    auto dot = s.find('.');
    std::string_view intPart = s.substr(0, dot);
    std::string_view fracPart = dot == std::string_view::npos ? std::string_view{} : s.substr(dot + 1, 4);

    uint64_t whole{};
    if (!intPart.empty()) {
        auto end{ intPart.data() + intPart.size() };
        auto [ptr, ec] = std::from_chars(intPart.data(), end, whole);
        if (ec != std::errc() || ptr != end)
            throw std::invalid_argument("string was malformed");
    }

    uint64_t frac{};
    if (!fracPart.empty()) {
        auto end{ fracPart.data() + fracPart.size() };
        auto [ptr, ec] = std::from_chars(fracPart.data(), end, frac);
        if (ec != std::errc() || ptr != end)
            throw std::invalid_argument("string was malformed");
        for (auto i{fracPart.size()}; i < 4; ++i) // pad to 4 digits if less
            frac *= 10;
    }

    return whole * 10000 + frac;
}

int getFixedPointPrecision(const std::string_view s) {
    auto dot = s.find('.');
    if (dot == std::string_view::npos)
        return 0;
    return static_cast<int>(s.size() - 1 - dot);
}
