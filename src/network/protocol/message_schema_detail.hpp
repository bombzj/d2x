#pragma once
#include "message_schema.hpp"

namespace d2x::net::protocol::detail {
#include "lod113c_lengths.inc"
constexpr ServerWireDescriptor serverWireDescriptor(ServerMessage message, std::string_view name) {
    const auto id = uint8_t(message);
    const auto length = serverLengths[id];
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
        if (present[id] || !serverLengths[id]) return false;
        present[id] = true;
    }
    for (size_t id = 0; id < std::size(serverLengths); ++id)
        if (bool(serverLengths[id]) != present[id]) return false;
    return true;
}());
}
