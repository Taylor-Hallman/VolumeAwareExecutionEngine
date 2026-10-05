#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

struct TradeTick {
    uint64_t ts_ns;
    int64_t px;
    uint32_t qty;
};

static constexpr auto VWAP_CAPACITY{ 65536uz };

template <std::size_t SIZE>
concept IsPowTwo = std::popcount(SIZE) == 1 && SIZE > 0;

template<std::size_t Capacity>
    requires IsPowTwo<Capacity>
class RollingVwap {
private:
    uint64_t m_windowNs;
    std::vector<TradeTick> m_ticks;
    std::size_t m_head{}, m_tail{}, m_count{};
    __int128 m_sumPxQty{};
    uint64_t m_sumQty{};

    std::mutex m_mutex;

public:
    explicit RollingVwap(uint64_t windowMs) : m_windowNs{windowMs * 1'000'000}, m_ticks{Capacity} {}

    void addTick(uint64_t ts_ns, int64_t px, uint32_t qty) {
        std::scoped_lock lock{ m_mutex };
        // evict stale data
        while (m_count > 0 && ts_ns - (m_ticks[m_head]).ts_ns > m_windowNs) {
            m_sumPxQty -= static_cast<__int128>(m_ticks[m_head].px) * static_cast<__int128>(m_ticks[m_head].qty);
            m_sumQty -= m_ticks[m_head].qty;
            m_head = (m_head + 1) & (Capacity - 1);
            --m_count;
        }

        if (m_count == Capacity) {
            // buffer full: must evict the oldest entry by capacity pressure,
            // even though it may still be within the time window
            m_sumPxQty -= static_cast<__int128>(m_ticks[m_head].px) * static_cast<__int128>(m_ticks[m_head].qty);
            m_sumQty -= m_ticks[m_head].qty;
            m_head = (m_head + 1) & (Capacity - 1);
            --m_count;
        }

        m_ticks[m_tail] = TradeTick{ ts_ns, px, qty };
        m_tail = (m_tail + 1) & (Capacity - 1);

        m_sumPxQty += static_cast<__int128>(px) * static_cast<__int128>(qty);
        m_sumQty += qty;

        ++m_count;

    }

    [[nodiscard]] int64_t getVwap() const {
        if (m_sumQty == 0)
            return 0;
        return static_cast<int64_t>((m_sumPxQty + m_sumQty / 2) / m_sumQty);
    }

    [[nodiscard]] std::size_t getTickCount() const {
        return m_count;
    }
};
