#include "RollingVwap.h"

RollingVwap::RollingVwap(uint64_t windowMs) : m_windowNs{windowMs * 1'000'000},
    m_ticks{ CAPACITY } {}

void RollingVwap::addTick(uint64_t ts_ns, int64_t px, uint32_t qty) {
    // evict stale data
    while (m_count > 0 && ts_ns - (m_ticks[m_head]).ts_ns > m_windowNs) {
        m_sumPxQty -= static_cast<__int128>(m_ticks[m_head].px) * static_cast<__int128>(m_ticks[m_head].qty);
        m_sumQty -= m_ticks[m_head].qty;
        m_head = (m_head + 1) & (CAPACITY - 1);
        --m_count;
    }

    if (m_count == CAPACITY) {
        // buffer full: must evict the oldest entry by capacity pressure,
        // even though it may still be within the time window
        m_sumPxQty -= static_cast<__int128>(m_ticks[m_head].px) * static_cast<__int128>(m_ticks[m_head].qty);
        m_sumQty -= m_ticks[m_head].qty;
        m_head = (m_head + 1) & (CAPACITY - 1);
        --m_count;
    }

    m_ticks[m_tail] = TradeTick{ ts_ns, px, qty };
    m_tail = (m_tail + 1) & (CAPACITY - 1);

    m_sumPxQty += px * qty;
    m_sumQty += qty;

    ++m_count;
}

int64_t RollingVwap::getVwap() const {
    if (m_sumQty == 0)
        return 0;
    return static_cast<int64_t>(m_sumPxQty / m_sumQty);
}
