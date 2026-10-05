#pragma once
#include "core/bytes.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace d2x::net::protocol {
class ProtocolError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class Reader {
  public:
    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}
    size_t remaining() const { return bytes_.size() - cursor_; }
    uint8_t u8() { require(1); return bytes_[cursor_++]; }
    uint16_t u16() { const auto lo = u8(); return uint16_t(lo | uint16_t(u8()) << 8); }
    uint16_t be16() { const auto hi = u8(); return uint16_t(uint16_t(hi) << 8 | u8()); }
    uint32_t u32() { const auto lo = u16(); return uint32_t(lo) | uint32_t(u16()) << 16; }
    uint64_t u64() { const auto lo = u32(); return uint64_t(lo) | uint64_t(u32()) << 32; }
    std::span<const uint8_t> take(size_t count) {
        require(count); auto value = bytes_.subspan(cursor_, count); cursor_ += count; return value;
    }
    std::string string(size_t limit = 1024) {
        auto rest = bytes_.subspan(cursor_);
        auto end = std::find(rest.begin(), rest.end(), uint8_t{});
        const auto length = size_t(end - rest.begin());
        if (end == rest.end() || length > limit) throw ProtocolError("Invalid protocol string");
        std::string value(rest.begin(), end); cursor_ += length + 1; return value;
    }
    void finish() const { if (remaining()) throw ProtocolError("Unexpected packet trailing bytes"); }
  private:
    std::span<const uint8_t> bytes_;
    size_t cursor_{};
    void require(size_t count) const { if (count > remaining()) throw ProtocolError("Truncated packet"); }
};
class Writer {
  public:
    void u8(uint8_t value) { bytes_.push_back(value); }
    void u16(uint16_t value) { u8(uint8_t(value)); u8(uint8_t(value >> 8)); }
    void u32(uint32_t value) { u16(uint16_t(value)); u16(uint16_t(value >> 16)); }
    void u64(uint64_t value) { u32(uint32_t(value)); u32(uint32_t(value >> 32)); }
    void append(std::span<const uint8_t> value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }
    void string(std::string_view value, size_t limit = 1024) {
        if (value.size() > limit || value.find('\0') != std::string_view::npos)
            throw ProtocolError("Invalid outgoing protocol string");
        bytes_.insert(bytes_.end(), value.begin(), value.end()); u8(0);
    }
    Bytes release() { return std::move(bytes_); }
  private:
    Bytes bytes_;
};
struct Packet {
    uint8_t id{};
    Bytes body;
};
enum class Framing { Sid, Mcp };
Bytes frame(Framing framing, uint8_t id, std::span<const uint8_t> body);
class PacketStream {
  public:
    explicit PacketStream(Framing framing, size_t bufferedLimit = 256 * 1024)
        : framing_(framing), limit_(bufferedLimit) {}
    void append(std::span<const uint8_t> bytes);
    bool next(Packet &packet);
    void reset() { bytes_.clear(); offset_ = 0; }
    bool empty() const { return offset_ == bytes_.size(); }
  private:
    Framing framing_;
    size_t limit_;
    Bytes bytes_;
    size_t offset_{};
};
} // namespace d2x::net::protocol
