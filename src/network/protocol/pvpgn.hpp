#pragma once
#include "wire.hpp"

namespace d2x::net::protocol {
struct PvpgnPacket {
    uint16_t type{};
    uint32_t sequence{};
    Bytes body;
};
inline Bytes pvpgnFrame(uint16_t type, uint32_t sequence, std::span<const uint8_t> body) {
    if (body.size() > UINT16_MAX - 8) throw ProtocolError("PvPGN packet exceeds 16-bit length");
    Writer out;
    out.u16(uint16_t(body.size() + 8)); out.u16(type); out.u32(sequence); out.append(body);
    return out.release();
}
class PvpgnStream {
    Bytes bytes_;
    size_t offset_{};
  public:
    void append(std::span<const uint8_t> bytes) {
        constexpr size_t limit = 256 * 1024;
        if (bytes.size() > limit || bytes_.size() - offset_ > limit - bytes.size())
            throw ProtocolError("PvPGN input exceeds limit");
        if (offset_) { bytes_.erase(bytes_.begin(), bytes_.begin() + offset_); offset_ = 0; }
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }
    bool next(PvpgnPacket &packet) {
        const auto available = std::span<const uint8_t>(bytes_).subspan(offset_);
        if (available.size() < 2) return false;
        Reader in(available);
        const auto size = in.u16();
        if (size < 8) throw ProtocolError("Invalid PvPGN frame length");
        if (available.size() < size) return false;
        packet.type = in.u16(); packet.sequence = in.u32();
        const auto body = in.take(size - 8);
        packet.body.assign(body.begin(), body.end()); offset_ += size;
        return true;
    }
};
}