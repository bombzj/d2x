#pragma once
#include "network/protocol/wire.hpp"

namespace d2x::net::protocol {
class BitWriter {
    Bytes bytes_;
    size_t cursor_{};
  public:
    void write(uint32_t value, unsigned count) {
        if (count > 32 || (count < 32 && (value >> count))) throw ProtocolError("Packed value exceeds bit width");
        for (unsigned bit = 0; bit < count; ++bit, ++cursor_) {
            if (cursor_ / 8 == bytes_.size()) bytes_.push_back(0);
            bytes_[cursor_ / 8] |= uint8_t(((value >> bit) & 1) << (cursor_ % 8));
        }
    }
    Bytes release() { return std::move(bytes_); }
};
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
