#include <gtest/gtest.h>

#include "data/GCMD_1/Quote.h"
#include "util/CSVParser.h"

// --- CSV parser tests ---

TEST(ParserTest, FixedPointPrice_TwoDecimalDigits) {
    EXPECT_EQ(parseFixedPointPrice("87.37"), 873700);
}

TEST(ParserTest, FixedPointPrice_WholeNumberNoDecimal) {
    EXPECT_EQ(parseFixedPointPrice("100"), 1000000);
}

TEST(ParserTest, FixedPointPrice_FourDecimalDigits) {
    EXPECT_EQ(parseFixedPointPrice("101.2500"), 1012500);
}

TEST(ParserTest, FixedPointPrice_SingleDecimalDigit) {
    EXPECT_EQ(parseFixedPointPrice("34.2"), 342000);
}

TEST(ParserTest, TimeToNsSinceOpen_ExactlyAnchor) {
    EXPECT_EQ(parseTimeToNsSinceOpen("09:30:00.000"), 0uz);
}

TEST(ParserTest, TimeToNsSinceOpen_SmallOffset) {
    // 500ms after open = 500,000,000 ns
    EXPECT_EQ(parseTimeToNsSinceOpen("09:30:00.500"), 500'000'000uz);
}

TEST(ParserTest, TimeToNsSinceOpen_CrossesHourAndMinuteBoundary) {
    // 09:30:00.000 -> 10:31:15.250
    // delta: 1h 1m 15.250s = 3675.250s = 3,675,250,000,000 ns
    EXPECT_EQ(parseTimeToNsSinceOpen("10:31:15.250"), 3'675'250'000'000uz);
}

TEST(ParserTest, ParseQuotesAndTrades_EndToEnd) {
    auto data = parseQuotesAndTrades("Quotes_and_Trades.csv");

    ASSERT_FALSE(data.empty());

    // Spot-check the first row against known sample data
    ASSERT_TRUE(std::holds_alternative<Quote>(data[0]));
    auto& firstQuote = std::get<Quote>(data[0]);
    EXPECT_EQ(std::string(firstQuote.symbol.data(), 6), "SYNTH3");
}
