#pragma once
#include "network/protocol/wire.hpp"

namespace d2x::hosting {
enum class MessageDomain { Lifecycle, Movement, Combat, Inventory, Interaction, Progression, Social, World, Character };
enum class MessageSupport { Stub, AdmissionOnly, Implemented };
enum class GamePhase { Connected, LoggedOn, Entered, Closed };
enum class ClientMessage : uint8_t {
#define D2X_MESSAGE(id, name, length, domain, phase, support) name = id,
#include "client_messages.inc"
#undef D2X_MESSAGE
};
enum class ServerMessage : uint8_t {
#define D2X_MESSAGE(id, name, domain, support) name = id,
#include "server_messages.inc"
#undef D2X_MESSAGE
};
struct MessageDescriptor {
    uint8_t id;
    std::string_view name;
    MessageDomain domain;
    MessageSupport support;
    size_t fixedSize{};
    GamePhase phase = GamePhase::Entered;
};
struct SubmessageDescriptor {
    ClientMessage packet;
    uint32_t selector;
    std::string_view name;
    MessageDomain domain;
    MessageSupport support;
};
std::span<const SubmessageDescriptor> submessages();
std::span<const MessageDescriptor> clientMessages();
std::span<const MessageDescriptor> serverMessages();
std::span<const MessageDescriptor> realmRequests();
std::span<const MessageDescriptor> realmResponses();
const MessageDescriptor &clientMessage(uint8_t);
const MessageDescriptor &serverMessage(uint8_t);
std::string_view domainName(MessageDomain);
std::string_view supportName(MessageSupport);
// Zero means incomplete. Unregistered opcodes are errors, never guessed/skipped.
size_t clientPacketSize(std::span<const uint8_t>);
void requirePhase(const MessageDescriptor &, GamePhase);
// Reuses the SAME 1.13c S2C framer as the client. No second output length table.
void validateServerPacket(std::span<const uint8_t>);
// One typed output entry for every registered S2C message. A stub refuses to
// serialize before invoking its body encoder; it cannot produce an empty ACK.
template<class Encode> Bytes encodeServerPacket(ServerMessage message, Encode encode) {
    const auto &entry = serverMessage(uint8_t(message));
    if (entry.support == MessageSupport::Stub)
        throw net::protocol::ProtocolError("Server serializer is not implemented: " + std::string(entry.name));
    net::protocol::Writer out;
    out.u8(uint8_t(message)); encode(out);
    auto packet = out.release(); validateServerPacket(packet);
    return packet;
}
}
