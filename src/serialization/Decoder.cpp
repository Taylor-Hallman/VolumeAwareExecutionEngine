#include "Decoder.h"

#include <algorithm>
#include <stdexcept>

#include "../data/GCMD_1/FrameHeader.h"
#include "MsgType.h"

using BytesIterator = std::span<const std::byte>::iterator;

static constexpr size_t SYMBOL_SIZE{12uz};

template<std::integral T>
static T read_le(BytesIterator& it, const BytesIterator buf_end) {
    if (std::distance(it, buf_end) < sizeof(T))
        throw std::invalid_argument("Incorrect data size");

    T value{};
    auto end{it + sizeof(T)};
    for (auto i{0uz}; it != end; ++it, ++i)
        value |= static_cast<T>(*it) << 8 * i;
    return value;
}

DecodeResult Decode(const std::span<const std::byte> data) {
    DecodeResult NoOp{ .message = std::nullopt, .bytesConsumed = 0uz };
    if (data.size() < FrameHeader::SIZE)
        return NoOp;

    auto it{data.begin()};
    const auto data_end{data.end()};

    FrameHeader header{
        .body_len = read_le<uint16_t>(it, data_end),
        .msg_type = read_le<uint8_t>(it, data_end),
        .version = read_le<uint8_t>(it, data_end)
    };

    switch (header.msg_type) {
        case HEARTBEAT: {
            if (data.size() < FrameHeader::SIZE + Heartbeat::SIZE)
                return NoOp;
            if (header.body_len != Heartbeat::SIZE)
                return NoOp;

            return DecodeResult{
                .message = Heartbeat{ read_le<uint64_t>(it, data_end) },
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        }
        break;
        case QUOTE: {
            if (data.size() < FrameHeader::SIZE + Quote::SIZE)
                return NoOp;
            if (header.body_len != Quote::SIZE)
                return NoOp;

            std::array<char, SYMBOL_SIZE> symbol{};
            size_t i{};
            std::for_each(it, it + SYMBOL_SIZE, [&symbol, &i](auto &b) {
                symbol[i++] = static_cast<char>(b);
            });
            std::advance(it, SYMBOL_SIZE);

            Quote quote{
                .symbol = symbol,
                .ts_ns = read_le<uint64_t>(it, data_end),
                .bid_qty = read_le<uint32_t>(it, data_end),
                .bid_px = read_le<int64_t>(it, data_end),
                .ask_qty = read_le<uint32_t>(it, data_end),
                .ask_px = read_le<int64_t>(it, data_end)
            };

            return DecodeResult{
                .message = quote,
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        }
        break;
        case SESSION_CONTROL: {
            if (data.size() < FrameHeader::SIZE + SessionControl::SIZE)
                return NoOp;
            if (header.body_len != SessionControl::SIZE)
                return NoOp;

            SessionControl sc{
                .ts_ns = read_le<uint64_t>(it, data_end),
                .state = read_le<uint8_t>(it, data_end)
            };

            return DecodeResult{
                .message = sc,
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        } break;
        case TRADE: {
            if (data.size() < FrameHeader::SIZE + Trade::SIZE)
                return NoOp;
            if (header.body_len != Trade::SIZE)
                return NoOp;

            std::array<char, SYMBOL_SIZE> symbol{};
            size_t i{};
            std::for_each(it, it + SYMBOL_SIZE, [&symbol, &i](auto &b) {
                symbol[i++] = static_cast<char>(b);
            });
            std::advance(it, SYMBOL_SIZE);

            Trade trade{
                .symbol = symbol,
                .ts_ns = read_le<uint64_t>(it, data_end),
                .qty = read_le<uint32_t>(it, data_end),
                .px = read_le<int64_t>(it, data_end),
                .aggressor = read_le<char>(it, data_end),
                .id = read_le<int64_t>(it, data_end)
            };

            return DecodeResult{
                .message = trade,
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        } break;
        case NEW_ORDER: {
            if (data.size() < FrameHeader::SIZE + NewOrder::SIZE)
                return NoOp;
            if (header.body_len != NewOrder::SIZE)
                return NoOp;

            auto client_order_id{ read_le<uint64_t>(it, data_end) };

            std::array<char, SYMBOL_SIZE> symbol{};
            size_t i{};
            std::for_each(it, it + SYMBOL_SIZE, [&symbol, &i](auto &b) {
                symbol[i++] = static_cast<char>(b);
            });
            std::advance(it, SYMBOL_SIZE);

            NewOrder new_order{
                .client_order_id = client_order_id,
                .symbol = symbol,
                .status = read_le<char>(it, data_end),
                .ts_ns = read_le<uint64_t>(it, data_end),
                .trade_id = read_le<int64_t>(it, data_end),
                .side = read_le<char>(it, data_end),
                .qty = read_le<uint32_t>(it, data_end),
                .limit_px = read_le<int64_t>(it, data_end)
            };

            return DecodeResult{
                .message = new_order,
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        } break;
        case EXEC_REPORT: {
            if (data.size() < FrameHeader::SIZE + ExecReport::SIZE)
                return NoOp;
            if (header.body_len != ExecReport::SIZE)
                return NoOp;

            ExecReport report{
                .client_order_id = read_le<uint64_t>(it, data_end),
                .ts_ns = read_le<uint64_t>(it, data_end),
                .status = read_le<uint8_t>(it, data_end),
                .filled_qty = read_le<uint32_t>(it, data_end),
                .avg_px = read_le<int64_t>(it, data_end),
                .reason_code = read_le<uint8_t>(it, data_end)
            };

            return DecodeResult{
                .message = report,
                .bytesConsumed = static_cast<size_t>(FrameHeader::SIZE + header.body_len)
            };
        } break;
        default:
            throw std::invalid_argument("Invalid Message Type");
    }
}