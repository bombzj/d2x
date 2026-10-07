#include "network/protocol/d2gs_stream.hpp"
#include <algorithm>

namespace d2x::net::protocol {
namespace {
constexpr size_t bufferLimit = 256 * 1024;
void append_bounded(Bytes &buffer, std::span<const uint8_t> bytes) {
    if (bytes.size() > bufferLimit || buffer.size() > bufferLimit - bytes.size())
        throw ProtocolError("D2GS receive buffer limit exceeded");
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
}
}
void D2gsStream::append(std::span<const uint8_t> bytes) { append_bounded(wire_, bytes); }
void D2gsStream::reset() { mode_ = Mode::Negotiation; wire_.clear(); logical_.clear(); }
bool D2gsStream::next(Packet &packet) {
    for (;;) {
        if (mode_ == Mode::Negotiation) {
            if (wire_.size() < 2) return false;
            if (wire_[0] != 0xAF || wire_[1] > 1)
                throw ProtocolError("Unsupported D2GS compression negotiation");
            packet = {0xAF, {wire_[1]}};
            mode_ = wire_[1] ? Mode::Huffman : Mode::Raw;
            wire_.erase(wire_.begin(), wire_.begin() + 2);
            return true;
        }
        if (!logical_.empty()) {
            const size_t length = lod113c_packet_size(logical_);
            if (length && length <= logical_.size()) {
                packet = {logical_[0], Bytes(logical_.begin() + 1, logical_.begin() + length)};
                logical_.erase(logical_.begin(), logical_.begin() + length);
                if (packet.id == 0xAF) {
                    if (!logical_.empty()) throw ProtocolError("D2GS compression change inside envelope");
                    mode_ = packet.body[0] ? Mode::Huffman : Mode::Raw;
                }
                return true;
            }
        }
        if (mode_ == Mode::Raw) {
            if (wire_.empty()) return false;
            append_bounded(logical_, wire_);
            wire_.clear();
            continue;
        }
        if (wire_.empty()) return false;
        const size_t header = wire_[0] < 0xF0 ? 1 : 2;
        if (wire_.size() < header) return false;
        const size_t length = header == 1 ? wire_[0] : ((wire_[0] & 0x0F) << 8) | wire_[1];
        if (length <= header) throw ProtocolError("Invalid D2GS compression envelope length");
        if (wire_.size() < length) return false;
        const auto decompressed = decompress_huffman(std::span<const uint8_t>(wire_).subspan(header, length - header));
        if (decompressed.empty()) throw ProtocolError("Empty D2GS compression envelope");
        append_bounded(logical_, decompressed);
        wire_.erase(wire_.begin(), wire_.begin() + length);
    }
}
Bytes game_logon(uint32_t hash, uint16_t token, uint8_t characterClass,
    uint8_t locale, std::string_view name) {
    if (characterClass > 6 || name.empty() || name.size() > 15 ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c >= 127; }))
        throw ProtocolError("Invalid LoD game logon character");
    Writer out;
    out.u8(0x68); out.u32(hash); out.u16(token); out.u8(characterClass); out.u32(13);
    // Expansion signature from the user's 1.13c D2Client.dll logon encoder.
    // Its DWORD order differs from the reference JS example's u64 array.
    out.u32(0xED5DCC50); out.u32(0x91A519B6); out.u8(locale);
    std::array<uint8_t, 16> padded{};
    std::copy(name.begin(), name.end(), padded.begin()); out.append(padded);
    return out.release();
}
Bytes game_ping(uint32_t elapsed, uint32_t latency) {
    Writer out; out.u8(0x6D); out.u32(elapsed); out.u32(latency); out.u32(0); return out.release();
}
Bytes game_chat(std::string_view text) {
    if (text.empty() || text.size() > 255 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c < 127; }))
        throw ProtocolError("Chat requires 1-255 printable ASCII bytes");
    // D2MOO D2PacketDef::Clt15 and D2Net::SERVER_GetClientPacketSize:
    // ID, message type, language, message\0, receiver\0, extension length.
    Writer out;
    out.u8(0x15); out.u8(1); out.u8(0);
    out.string(text, 255); out.string({}, 15); out.u8(0);
    return out.release();
}
} // namespace d2x::net::protocol
