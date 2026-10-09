#include "network/protocol/d2gs_stream.hpp"
#include "network/protocol/message_schema.hpp"
#include <algorithm>
#include <iterator>

namespace d2x::net::protocol {
namespace {
size_t byte_length(std::span<const uint8_t> bytes, size_t offset, size_t minimum) {
    if (bytes.size() <= offset) return 0;
    const size_t length = bytes[offset];
    if (length < minimum) throw ProtocolError("Invalid variable D2GS packet length");
    return length;
}
size_t word_length(std::span<const uint8_t> bytes, size_t offset, size_t minimum) {
    if (bytes.size() < offset + 2) return 0;
    Reader in(bytes.subspan(offset)); const auto length = in.u16();
    if (length < minimum) throw ProtocolError("Invalid variable D2GS packet length");
    return length;
}
}
size_t lod113c_client_packet_size(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return 0;
    const auto *entry = findClientWireMessage(bytes[0]);
    if (!entry) throw ProtocolError("Undefined LoD 1.13c client packet ID: " + std::to_string(bytes[0]));
    if (entry->fixedSize) return bytes.size() >= entry->fixedSize ? entry->fixedSize : 0;
    if (entry->message != ClientMessage::Chat) throw ProtocolError("Missing native client packet framer");
    if (bytes.size() < 3) return 0;
    size_t offset = 3;
    for (const size_t maximum : {size_t{255}, size_t{15}}) {
        const auto end = bytes.begin() + std::min(bytes.size(), offset + maximum + 1);
        const auto terminator = std::find(bytes.begin() + offset, end, uint8_t{});
        if (terminator == end) {
            if (bytes.size() >= offset + maximum + 1) throw ProtocolError("Unterminated client chat string");
            return 0;
        }
        offset = size_t(terminator - bytes.begin()) + 1;
    }
    if (offset == bytes.size()) return 0;
    const auto length = offset + 1 + bytes[offset];
    return bytes.size() >= length ? length : 0;
}
void validateClientPacket(std::span<const uint8_t> bytes) {
    if (bytes.empty() || lod113c_client_packet_size(bytes) != bytes.size())
        throw ProtocolError("Invalid outgoing native client packet size");
}
size_t lod113c_packet_size(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return 0;
    const uint8_t id = bytes[0];
    const auto *entry = findServerWireMessage(id);
    if (!entry)
        throw ProtocolError("Undefined LoD 1.13c server packet ID: " + std::to_string(id));
    if (entry->framing == WireFraming::Fixed) return entry->fixedSize;
    if (entry->framing == WireFraming::ByteLength) return byte_length(bytes, entry->lengthOffset, entry->minimumSize);
    if (entry->framing == WireFraming::WordLength) return word_length(bytes, entry->lengthOffset, entry->minimumSize);
    switch (id) {
    case 0x26: {
        if (bytes.size() < 10) return 0;
        // D2PacketDef/SCmd: at most 15 sender bytes and 255 message bytes.
        // Bound each search even when multiple logical packets share a buffer.
        const auto nameEnd = bytes.begin() + std::min(bytes.size(), size_t{26});
        const auto first = std::find(bytes.begin() + 10, nameEnd, uint8_t{});
        if (first == nameEnd) {
            if (bytes.size() >= 26) throw ProtocolError("Unterminated D2GS chat sender");
            return 0;
        }
        const auto messageOffset = size_t(first + 1 - bytes.begin());
        const auto messageEnd = bytes.begin() + std::min(bytes.size(), messageOffset + 256);
        const auto second = std::find(first + 1, messageEnd, uint8_t{});
        if (second == messageEnd) {
            if (bytes.size() >= messageOffset + 256) throw ProtocolError("Unterminated D2GS chat message");
            return 0;
        }
        return size_t(second - bytes.begin()) + 1;
    }
    case 0x94: return bytes.size() < 2 ? 0 : 6 + 3 * size_t(bytes[1]);
    case 0xAF:
        if (bytes.size() < 2) return 0;
        if (bytes[1] > 1) throw ProtocolError("Unsupported D2GS compression mode");
        return 2; // In 1.11+, AF is compression info; the 1.10 raw marker is incompatible.
    // Warden is disabled in the reference deployment; no guessed crypto or length.
    case 0xAE: throw ProtocolError("Warden packets are unsupported");
    default: throw ProtocolError("Unverified variable LoD 1.13c packet length, ID: " + std::to_string(id));
    }
}
} // namespace d2x::net::protocol
