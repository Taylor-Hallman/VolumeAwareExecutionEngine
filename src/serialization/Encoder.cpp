#include "Encoder.h"

#include <algorithm>

#include "../data/GCMD_1/FrameHeader.h"

namespace {
    enum MsgType : uint8_t {
        QUOTE=1,
        TRADE=2,
        HEARTBEAT=3,
        SESSION_CONTROL=4,
        NEW_ORDER=10,
        EXEC_REPORT=11
    };
}

// Enforce little-endianness
template <std::integral T>
static void append_le(std::vector<std::byte>& buf, T value) {
    for (auto i{0uz}; i < sizeof(T); ++i)
        buf.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
}

std::vector<std::byte> Encode(const Heartbeat& hb) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = Heartbeat::SIZE,
        .msg_type = HEARTBEAT,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    append_le(data, hb.ts_ns);

    return data;
}

std::vector<std::byte> Encode(const Quote& quote) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = Quote::SIZE,
        .msg_type = QUOTE,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    std::ranges::for_each(quote.symbol, [&data](auto c) {
        data.push_back(static_cast<std::byte>(c));
    });
    append_le(data, quote.ts_ns);
    append_le(data, quote.bid_qty);
    append_le(data, quote.bid_px);
    append_le(data, quote.ask_qty);
    append_le(data, quote.ask_px);

    return data;
}

std::vector<std::byte> Encode(const SessionControl& sc) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = SessionControl::SIZE,
        .msg_type = SESSION_CONTROL,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    append_le(data, sc.ts_ns);
    append_le(data, sc.state);

    return data;
}

std::vector<std::byte> Encode(const Trade& trade) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = Trade::SIZE,
        .msg_type = TRADE,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    std::ranges::for_each(trade.symbol, [&data](auto c) {
        data.push_back(static_cast<std::byte>(c));
    });
    append_le(data, trade.ts_ns);
    append_le(data, trade.qty);
    append_le(data, trade.px);
    append_le(data, trade.aggressor);
    append_le(data, trade.id);

    return data;
}

std::vector<std::byte> Encode(const NewOrder& newOrder) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = NewOrder::SIZE,
        .msg_type = NEW_ORDER,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    append_le(data, newOrder.client_order_id);
    std::ranges::for_each(newOrder.symbol, [&data](auto c) {
        data.push_back(static_cast<std::byte>(c));
    });
    append_le(data, newOrder.status);
    append_le(data, newOrder.ts_ns);
    append_le(data, newOrder.trade_id);
    append_le(data, newOrder.side);
    append_le(data, newOrder.qty);
    append_le(data, newOrder.limit_px);

    return data;
}

std::vector<std::byte> Encode(const ExecReport& report) {
    std::vector<std::byte> data;
    FrameHeader header{
        .body_len = ExecReport::SIZE,
        .msg_type = EXEC_REPORT,
        .version = 1
    };

    append_le(data, header.body_len);
    append_le(data, header.msg_type);
    append_le(data, header.version);

    append_le(data, report.client_order_id);
    append_le(data, report.ts_ns);
    append_le(data, report.status);
    append_le(data, report.filled_qty);
    append_le(data, report.avg_px);
    append_le(data, report.reason_code);

    return data;
}