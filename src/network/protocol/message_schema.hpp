#pragma once
#include "network/protocol/wire.hpp"

namespace d2x::net::protocol {
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
const ServerWireDescriptor *findServerWireMessage(uint8_t id);
const ClientWireDescriptor *findClientWireMessage(uint8_t id);
std::string_view framingName(WireFraming framing);
size_t lod113c_client_packet_size(std::span<const uint8_t> bytes);
void validateClientPacket(std::span<const uint8_t> bytes);
}
