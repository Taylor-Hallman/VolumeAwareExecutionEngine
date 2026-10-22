#include "CSVParser.h"

#include <fstream>
#include <charconv>
#include <fcntl.h>
#include <filesystem>
#include <print>
#include <spanstream>
#include <sys/mman.h>
#include <sys/stat.h>

#include "data/GCMD_1/Trade.h"
#include "FixedPointParser.h"

static constexpr int IGNORE{15};
static constexpr int NUM_FIELDS{9};

static constexpr uint64_t SESSION_START_NS =
        (9ull * 3600 + 30ull * 60) * 1'000'000'000ull; // 9:30:00.000 in ns-since-midnight

uint64_t parseTimeToNsSinceOpen(const std::string_view ts) {
    int hh{}, mm{}, ss{}, ms{};

    std::from_chars(ts.data(), ts.data() + 2, hh);
    std::from_chars(ts.data() + 3, ts.data() + 5, mm);
    std::from_chars(ts.data() + 6, ts.data() + 8, ss);
    std::from_chars(ts.data() + 9, ts.data() + 12, ms);

    uint64_t total_ns = static_cast<uint64_t>(hh) * 3600 * 1'000'000'000ull
                        + static_cast<uint64_t>(mm) * 60 * 1'000'000'000ull
                        + static_cast<uint64_t>(ss) * 1'000'000'000ull
                        + static_cast<uint64_t>(ms) * 1'000'000ull;

    return total_ns - SESSION_START_NS;
}

namespace {
    class MappedFile {
    public:
        struct MappedFileDetails {
            constexpr static int InvalidDescriptor{-1};

            int Descriptor{InvalidDescriptor};
            char *begin{nullptr};
            std::size_t size{0};

            [[nodiscard]] bool isOpen() const { return Descriptor != InvalidDescriptor; }

            void close() const {
                if (Descriptor == InvalidDescriptor) {
                    return;
                }

                ::close(Descriptor);
            }
        };

        MappedFile(const std::filesystem::path &path) {
            const auto descriptor{open(path.string().c_str(), O_RDONLY)};

            if (descriptor == -1)
                throw std::system_error(errno, std::system_category(), "Could not open file");

            struct stat sb{};
            if (fstat(descriptor, &sb) == -1)
                throw std::system_error(errno, std::system_category(), "Could not stat file");

            const auto start{
                static_cast<char *>(
                    mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, descriptor, 0))
            };
            if (start == MAP_FAILED)
                throw std::system_error(errno, std::system_category(), "Could not mmap file");

            m_details = {
                .Descriptor = descriptor,
                .begin = start,
                .size = static_cast<std::size_t>(sb.st_size)
            };

            // tell the kernel what our read access pattern is: contiguous read, so aggressively prefetch pages
            madvise(m_details.begin, m_details.size, MADV_SEQUENTIAL);
        }

        [[nodiscard]] std::span<const char> Data() const {
            return {m_details.begin, m_details.size};
        }

        ~MappedFile() {
            if (!m_details.isOpen())
                return;

            m_details.close();
            if (m_details.begin == nullptr)
                return;

            munmap(m_details.begin, m_details.size);
        }

    private:
        MappedFileDetails m_details;
    };
} // namespace

std::vector<std::variant<Quote, Trade>> parseQuotesAndTrades(const std::string_view inputFileName) {
    const MappedFile file(inputFileName);

    std::vector<std::variant<Quote, Trade>> data;
    data.reserve(10'000);

    auto view{file.Data()};
    auto begin{view.begin()};

    // Ignore first part of the CSV and skip straight to the data
    for (int i{}; i < IGNORE; ++i) {
        const auto endl{std::ranges::find(begin, std::unreachable_sentinel, '\n')};
        begin = endl + 1;
    }

    while (begin != view.end()) {
        const auto endl{std::ranges::find(begin, std::unreachable_sentinel, '\n')};
        std::array<std::string_view, NUM_FIELDS> fields;
        // fields[0]=Timestamp, [1]=Type, [2]=Symbol, [3]=BidPrice, [4]=BidQty,
        // [5]=AskPrice, [6]=AskQty, [7]=Price, [8]=Qty

        for (auto i{0}; i < NUM_FIELDS; ++i) {
            auto comma{ std::ranges::find(begin, endl, ',')};
            fields[i] = { begin, comma };
            begin = comma + 1;
        }

        uint64_t ts_ns{parseTimeToNsSinceOpen(fields[0])};
        std::size_t n{ std::min(12uz, fields[2].size()) };
        std::array<char, 12> symbol{};
        std::copy_n(fields[2].data(), n, symbol.data());

        if (fields[1] == "Q") {
            int64_t bidPrice{static_cast<int64_t>(parseFixedPoint(fields[3]))};
            uint32_t bidQty;
            auto [ptr, ec] = std::from_chars(fields[4].begin(), fields[4].end(), bidQty);
            if (ec != std::errc{} || ptr != fields[4].end())
                throw std::runtime_error("Failed to parse bid qty");
            int64_t askPrice{static_cast<int64_t>(parseFixedPoint(fields[5]))};
            uint32_t askQty;
            auto [ptr2, ec2] = std::from_chars(fields[6].begin(), fields[6].end(), askQty);
            if (ec2 != std::errc{} || ptr2 != fields[6].end())
                throw std::runtime_error("Failed to parse ask qty");

            data.emplace_back(Quote{
                .symbol = symbol,
                .ts_ns = ts_ns,
                .bid_qty = bidQty,
                .bid_px = bidPrice,
                .ask_qty = askQty,
                .ask_px = askPrice
            });
        } else if (fields[1] == "T") {
            int64_t price{static_cast<int64_t>(parseFixedPoint(fields[7]))};
            uint32_t quantity;
            auto [ptr, ec] = std::from_chars(fields[8].begin(), fields[8].end(), quantity);
            if (ec != std::errc{} || ptr != fields[8].end())
                throw std::runtime_error("Failed to parse quantity");

            data.emplace_back(Trade{
                .symbol = symbol,
                .ts_ns = ts_ns,
                .qty = quantity,
                .px = price,
                .aggressor = '?' // '?' as the CSV doesn't contain this data
            });
        }

        begin = endl + 1;
    }

    return data;
}