#include "network/protocol/wire.hpp"
#include <limits>

namespace d2x::net::protocol {
Bytes frame(Framing framing, uint8_t id, std::span<const uint8_t> body) {
    const size_t header = framing == Framing::Sid ? 4 : 3;
    if (body.size() > std::numeric_limits<uint16_t>::max() - header)
        throw ProtocolError("Outgoing packet exceeds wire length");
    Writer out;
    if (framing == Framing::Sid) { out.u8(0xFF); out.u8(id); }
    out.u16(uint16_t(body.size() + header));
    if (framing == Framing::Mcp) out.u8(id);
    out.append(body);
    return out.release();
}
void PacketStream::append(std::span<const uint8_t> bytes) {
    if (bytes.size() > limit_ || bytes_.size() - offset_ > limit_ - bytes.size())
        throw ProtocolError("Protocol receive buffer limit exceeded");
    if (offset_) { bytes_.erase(bytes_.begin(), bytes_.begin() + offset_); offset_ = 0; }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}
bool PacketStream::next(Packet &packet) {
    const size_t header = framing_ == Framing::Sid ? 4 : 3;
    const auto available = std::span<const uint8_t>(bytes_).subspan(offset_);
    if (available.size() < header) return false;
    Reader reader(available);
    uint8_t id{};
    if (framing_ == Framing::Sid) {
        if (reader.u8() != 0xFF) throw ProtocolError("Invalid SID packet marker");
        id = reader.u8();
    }
    const auto length = reader.u16();
    if (framing_ == Framing::Mcp) id = reader.u8();
    if (length < header || length > limit_) throw ProtocolError("Invalid framed packet length");
    if (available.size() < length) return false;
    const auto body = available.subspan(header, length - header);
    packet = {id, Bytes(body.begin(), body.end())};
    offset_ += length;
    if (offset_ == bytes_.size()) reset();
    return true;
}
} // namespace d2x::net::protocol
