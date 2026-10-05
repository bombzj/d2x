#pragma once
#include "network/protocol/wire.hpp"

namespace d2x::net::protocol {
// Original D2 packets pack least significant bits first, without byte alignment.
class BitReader {
    std::span<const uint8_t> bytes_;
    size_t cursor_{};

  public:
    explicit BitReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    uint32_t read(unsigned count) {
        if (count > 32 || count > bytes_.size() * 8 - cursor_)
            throw ProtocolError("Truncated packed game data");
        uint32_t value = 0;
        for (unsigned bit = 0; bit < count; ++bit, ++cursor_)
            value |= uint32_t((bytes_[cursor_ / 8] >> (cursor_ % 8)) & 1) << bit;
        return value;
    }
};
} // namespace d2x::net::protocol
