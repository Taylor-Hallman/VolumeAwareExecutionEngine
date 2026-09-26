#include "Encoder.h"

#include <algorithm>
#include <stdexcept>

#include "../data/GCMD_1/FrameHeader.h"
#include "MsgType.h"

// Enforce little-endianness
template <std::integral T>
static void append_le(std::span<std::byte>& buf, T value, size_t& i) {
    if (buf.size() - i < sizeof(T))
        throw std::invalid_argument("Incorrect data size");

    for (int j{0}; j < sizeof(T); ++i, ++j)
        buf[i] = static_cast<std::byte>((value >> (8 * j)) & 0xFF);
}

size_t Encode(const Heartbeat& hb, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = Heartbeat::SIZE,
        .msg_type = HEARTBEAT,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    append_le(dest, hb.ts_ns, i);

    return FrameHeader::SIZE + header.body_len;
}

size_t Encode(const Quote& quote, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = Quote::SIZE,
        .msg_type = QUOTE,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    std::ranges::for_each(quote.symbol, [&dest, &i](auto c) {
        dest[i++] = static_cast<std::byte>(c);
    });
    append_le(dest, quote.ts_ns, i);
    append_le(dest, quote.bid_qty, i);
    append_le(dest, quote.bid_px, i);
    append_le(dest, quote.ask_qty, i);
    append_le(dest, quote.ask_px, i);

    return FrameHeader::SIZE + header.body_len;
}

size_t Encode(const SessionControl& sc, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = SessionControl::SIZE,
        .msg_type = SESSION_CONTROL,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    append_le(dest, sc.ts_ns, i);
    append_le(dest, sc.state, i);

    return FrameHeader::SIZE + header.body_len;
}

size_t Encode(const Trade& trade, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = Trade::SIZE,
        .msg_type = TRADE,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    std::ranges::for_each(trade.symbol, [&dest, &i](auto c) {
        dest[i++] = static_cast<std::byte>(c);
    });
    append_le(dest, trade.ts_ns, i);
    append_le(dest, trade.qty, i);
    append_le(dest, trade.px, i);
    append_le(dest, trade.aggressor, i);
    append_le(dest, trade.id, i);

    return FrameHeader::SIZE + header.body_len;
}

size_t Encode(const NewOrder& newOrder, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = NewOrder::SIZE,
        .msg_type = NEW_ORDER,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    append_le(dest, newOrder.client_order_id, i);

    std::ranges::for_each(newOrder.symbol, [&dest, &i](auto c) {
        dest[i++] = static_cast<std::byte>(c);
    });
    append_le(dest, newOrder.status, i);
    append_le(dest, newOrder.ts_ns, i);
    append_le(dest, newOrder.trade_id, i);
    append_le(dest, newOrder.side, i);
    append_le(dest, newOrder.qty, i);
    append_le(dest, newOrder.limit_px, i);

    return FrameHeader::SIZE + header.body_len;
}

size_t Encode(const ExecReport& report, std::span<std::byte> dest) {
    FrameHeader header{
        .body_len = ExecReport::SIZE,
        .msg_type = EXEC_REPORT,
        .version = 1
    };

    size_t i{0uz};

    append_le(dest, header.body_len, i);
    append_le(dest, header.msg_type, i);
    append_le(dest, header.version, i);

    append_le(dest, report.client_order_id, i);
    append_le(dest, report.ts_ns, i);
    append_le(dest, report.status, i);
    append_le(dest, report.filled_qty, i);
    append_le(dest, report.avg_px, i);
    append_le(dest, report.reason_code, i);

    return FrameHeader::SIZE + header.body_len;
}