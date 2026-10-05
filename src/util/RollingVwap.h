#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

struct TradeTick {
    uint64_t ts_ns;
    int64_t px;
    uint32_t qty;
};

class RollingVwap {
private:
    static constexpr size_t CAPACITY{ 65536uz };
    static_assert((CAPACITY & (CAPACITY - 1)) == 0uz);
    uint64_t m_windowNs;
    std::vector<TradeTick> m_ticks;
    size_t m_head{}, m_tail{}, m_count{};
    __int128 m_sumPxQty{};
    uint64_t m_sumQty{};

    std::mutex m_mutex;

public:
    explicit RollingVwap(uint64_t windowMs);

    void addTick(uint64_t ts_ns, int64_t px, uint32_t qty);
    [[nodiscard]] int64_t getVwap() const;
};
