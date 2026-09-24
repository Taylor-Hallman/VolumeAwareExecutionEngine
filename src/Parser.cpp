#include "Parser.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <charconv>
#include <print>

#include "data/GCMD_1/Trade.h"

static constexpr int IGNORE{ 14 };
static constexpr int NUM_FIELDS{ 9 };

static constexpr uint64_t SESSION_START_NS =
    (9ull * 3600 + 30ull * 60) * 1'000'000'000ull; // 9:30:00.000 in ns-since-midnight

uint64_t parseTimeToNsSinceOpen(const std::string_view ts) {
    int hh{}, mm{}, ss{}, ms{};

    std::from_chars(ts.data(),      ts.data() + 2,  hh);
    std::from_chars(ts.data() + 3,  ts.data() + 5,  mm);
    std::from_chars(ts.data() + 6,  ts.data() + 8,  ss);
    std::from_chars(ts.data() + 9,  ts.data() + 12, ms);

    uint64_t total_ns = static_cast<uint64_t>(hh) * 3600 * 1'000'000'000ull
                       + static_cast<uint64_t>(mm) * 60 * 1'000'000'000ull
                       + static_cast<uint64_t>(ss) * 1'000'000'000ull
                       + static_cast<uint64_t>(ms) * 1'000'000ull;

    return total_ns - SESSION_START_NS;
}

int64_t parseFixedPointPrice(const std::string& s) {
    auto dot = s.find('.');
    std::string intPart = (dot == std::string::npos) ? s : s.substr(0, dot);
    std::string fracPart = (dot == std::string::npos) ? "" : s.substr(dot + 1);
    fracPart.resize(4, '0'); // pad or truncate to exactly 4 digits
    int64_t whole{ std::stoll(intPart) };
    int64_t frac{ std::stoll(fracPart) };
    return whole * 10000 + frac;
}

std::vector<std::variant<Quote, Trade>> parseQuotesAndTrades(const std::string_view inputFileName) {
    std::ifstream inputFile(inputFileName.data());

    std::vector<std::variant<Quote, Trade>> data;

    for (int i{}; i < IGNORE && inputFile.good(); ++i) {
        inputFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    std::string line;
    while (std::getline(inputFile, line)) {
        std::istringstream lineStream(line);

        std::array<std::string, NUM_FIELDS> fields;
        for (int i{}; i < NUM_FIELDS; ++i) {
            std::getline(lineStream, fields[i], ',');
        }
        // fields[0]=Timestamp, [1]=Type, [2]=Symbol, [3]=BidPrice, [4]=BidQty,
        // [5]=AskPrice, [6]=AskQty, [7]=Price, [8]=Qty

        uint64_t ts_ns{ parseTimeToNsSinceOpen(fields[0]) };
        size_t n = std::min(12uz, fields[2].size());
        std::array<char, 12> symbol{};
        std::copy_n(fields[2].data(), n, symbol.data());

        if (fields[1] == "Q") {
            int64_t bidPrice{ parseFixedPointPrice(fields[3]) };
            uint32_t bidQty{ static_cast<uint32_t>(std::stoi(fields[4])) };
            int64_t askPrice{ parseFixedPointPrice(fields[5]) };
            uint32_t askQty{ static_cast<uint32_t>(std::stoi(fields[6])) };

            data.emplace_back(Quote{
                .symbol = symbol,
                .ts_ns = ts_ns,
                .bid_qty = bidQty,
                .bid_px = bidPrice,
                .ask_qty = askQty,
                .ask_px = askPrice
            });
        }

        else if (fields[1] == "T") {
            int64_t price{ parseFixedPointPrice(fields[7] )};
            uint32_t quantity{ static_cast<uint32_t>(std::stoi(fields[8])) };

            data.emplace_back(Trade{
                .symbol = symbol,
                .ts_ns = ts_ns,
                .qty = quantity,
                .px = price,
                .aggressor = '?'
            });
        }
    }
    return data;
}