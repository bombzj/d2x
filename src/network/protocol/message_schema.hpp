#pragma once
#include "network/protocol/wire.hpp"

namespace d2x::net::protocol {
namespace detail {
#include "lod113c_lengths.inc"
}
enum class ServerMessage : uint8_t {
#define D2X_SERVER_MESSAGE(id, name) name = id,
#include "server_messages.inc"
#undef D2X_SERVER_MESSAGE
};
enum class WireFraming { Fixed, ByteLength, WordLength, Chat, SkillList, Compression, SavePart, Unsupported };
struct ServerWireDescriptor {
    ServerMessage message;
    std::string_view name;
    WireFraming framing;
    size_t fixedSize;
    size_t lengthOffset{};
    size_t minimumSize{};
};
constexpr ServerWireDescriptor serverWireDescriptor(ServerMessage message, std::string_view name) {
    const auto id = uint8_t(message);
    const auto length = detail::serverLengths[id];
    if (length > 0) return {message, name, WireFraming::Fixed, size_t(length)};
    switch (id) {
    case 0x16: return {message, name, WireFraming::WordLength, 0, 1, 13};
    case 0x26: return {message, name, WireFraming::Chat, 0};
    case 0x3E: return {message, name, WireFraming::ByteLength, 0, 1, 2};
    case 0x5B: return {message, name, WireFraming::WordLength, 0, 1, 34};
    case 0x94: return {message, name, WireFraming::SkillList, 0};
    case 0x9C: case 0x9D: return {message, name, WireFraming::ByteLength, 0, 2, 3};
    case 0xA6: return {message, name, WireFraming::WordLength, 0, 2, 4};
    case 0xA8: return {message, name, WireFraming::ByteLength, 0, 6, 8};
    case 0xAA: return {message, name, WireFraming::ByteLength, 0, 6, 7};
    case 0xAC: return {message, name, WireFraming::ByteLength, 0, 12, 13};
    case 0xAF: return {message, name, WireFraming::Compression, 0};
    case 0xB3: return {message, name, WireFraming::SavePart, 0, 1, 7};
    default: return {message, name, WireFraming::Unsupported, 0};
    }
}
inline constexpr ServerWireDescriptor serverWireMessages[]{
#define D2X_SERVER_MESSAGE(id, name) serverWireDescriptor(ServerMessage::name, #name),
#include "server_messages.inc"
#undef D2X_SERVER_MESSAGE
};
constexpr const ServerWireDescriptor *findServerWireMessage(uint8_t id) {
    for (const auto &entry : serverWireMessages)
        if (uint8_t(entry.message) == id) return &entry;
    return nullptr;
}
constexpr std::string_view framingName(WireFraming framing) {
    switch (framing) {
    case WireFraming::Fixed: return "fixed";
    case WireFraming::ByteLength: return "u8-length";
    case WireFraming::WordLength: return "u16-length";
    case WireFraming::Chat: return "chat-strings";
    case WireFraming::SkillList: return "skill-count";
    case WireFraming::Compression: return "compression";
    case WireFraming::SavePart: return "save-part";
    case WireFraming::Unsupported: return "unsupported";
    }
    return "unsupported";
}
enum class ClientMessage : uint8_t {
#define D2X_CLIENT_MESSAGE(id, name, size) name = id,
#include "client_messages.inc"
#undef D2X_CLIENT_MESSAGE
};
struct ClientWireDescriptor {
    ClientMessage message;
    std::string_view name;
    size_t fixedSize;
};
inline constexpr ClientWireDescriptor clientWireMessages[]{
#define D2X_CLIENT_MESSAGE(id, name, size) {ClientMessage::name, #name, size},
#include "client_messages.inc"
#undef D2X_CLIENT_MESSAGE
};
constexpr const ClientWireDescriptor *findClientWireMessage(uint8_t id) {
    for (const auto &entry : clientWireMessages)
        if (uint8_t(entry.message) == id) return &entry;
    return nullptr;
}
static_assert([] {
    std::array<bool, 256> present{};
    for (const auto &entry : clientWireMessages) {
        const auto id = uint8_t(entry.message);
        if (present[id]) return false;
        present[id] = true;
    }
    present.fill(false);
    for (const auto &entry : serverWireMessages) {
        const auto id = uint8_t(entry.message);
        if (present[id] || !detail::serverLengths[id]) return false;
        present[id] = true;
    }
    for (size_t id = 0; id < std::size(detail::serverLengths); ++id)
        if (bool(detail::serverLengths[id]) != present[id]) return false;
    return true;
}());
size_t lod113c_client_packet_size(std::span<const uint8_t> bytes);
void validateClientPacket(std::span<const uint8_t> bytes);
}
