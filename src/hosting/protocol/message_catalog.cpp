#include "message_catalog.hpp"
#include "network/protocol/d2gs_stream.hpp"
#include <iterator>

namespace d2x::hosting {
using net::protocol::ProtocolError;
namespace {
constexpr MessageDescriptor clients[]{
#define D2X_MESSAGE(id, name, length, domain, phase, support) {id, #name, MessageDomain::domain, MessageSupport::support, length, GamePhase::phase},
#include "client_messages.inc"
#undef D2X_MESSAGE
};
constexpr MessageDescriptor servers[]{
#define D2X_MESSAGE(id, name, domain, support) {id, #name, MessageDomain::domain, MessageSupport::support},
#include "server_messages.inc"
#undef D2X_MESSAGE
};
constexpr MessageDescriptor realm[]{
    {0x01, "Startup", MessageDomain::Lifecycle, MessageSupport::Implemented},
    {0x02, "CreateCharacter", MessageDomain::Character, MessageSupport::Implemented},
    {0x03, "CreateGame", MessageDomain::Lifecycle, MessageSupport::Implemented},
    {0x04, "JoinGame", MessageDomain::Lifecycle, MessageSupport::Implemented},
    {0x05, "ListGames", MessageDomain::Lifecycle, MessageSupport::Implemented},
    {0x06, "GameInfo", MessageDomain::Lifecycle, MessageSupport::Implemented},
    {0x07, "SelectCharacter", MessageDomain::Character, MessageSupport::Implemented},
    {0x0A, "DeleteCharacter", MessageDomain::Character, MessageSupport::Implemented},
    {0x19, "ListCharacters", MessageDomain::Character, MessageSupport::Implemented},
};
constexpr MessageDescriptor queue{0x14, "CreateQueue", MessageDomain::Lifecycle, MessageSupport::Stub};
constexpr auto responses = [] {
    std::array<MessageDescriptor, std::size(realm) + 1> result{};
    std::copy(std::begin(realm), std::end(realm), result.begin());
    result.back() = queue;
    return result;
}();
const MessageDescriptor &find(std::span<const MessageDescriptor> entries, uint8_t id) {
    for (const auto &entry : entries) if (entry.id == id) return entry;
    throw ProtocolError("Unregistered native packet ID: " + std::to_string(id));
}
}
std::span<const MessageDescriptor> clientMessages() { return clients; }
std::span<const MessageDescriptor> serverMessages() { return servers; }
std::span<const MessageDescriptor> realmRequests() { return realm; }
std::span<const MessageDescriptor> realmResponses() { return responses; }
std::span<const SubmessageDescriptor> submessages() {
    static constexpr SubmessageDescriptor entries[]{
        {ClientMessage::UiAction, 2, "CancelTrade", MessageDomain::Social, MessageSupport::Stub},
        {ClientMessage::UiAction, 3, "AcceptTrade", MessageDomain::Social, MessageSupport::Stub},
        {ClientMessage::UiAction, 4, "AgreeTrade", MessageDomain::Social, MessageSupport::Stub},
        {ClientMessage::UiAction, 7, "ResetTrade", MessageDomain::Social, MessageSupport::Stub},
        {ClientMessage::UiAction, 8, "OfferTradeGold", MessageDomain::Social, MessageSupport::Stub},
        {ClientMessage::UiAction, 18, "CloseStash", MessageDomain::Inventory, MessageSupport::Implemented},
        {ClientMessage::UiAction, 19, "WithdrawGold", MessageDomain::Inventory, MessageSupport::Implemented},
        {ClientMessage::UiAction, 20, "DepositGold", MessageDomain::Inventory, MessageSupport::Implemented},
        {ClientMessage::UiAction, 23, "CloseCube", MessageDomain::Inventory, MessageSupport::Stub},
        {ClientMessage::UiAction, 24, "Transmute", MessageDomain::Inventory, MessageSupport::Stub},
        {ClientMessage::NpcService, 0, "NpcTravel", MessageDomain::Interaction, MessageSupport::Stub},
        {ClientMessage::NpcService, 1, "OpenShop", MessageDomain::Interaction, MessageSupport::Implemented},
        {ClientMessage::NpcService, 2, "OpenGambleShop", MessageDomain::Interaction, MessageSupport::Stub},
    };
    return entries;
}
const MessageDescriptor &clientMessage(uint8_t id) { return find(clients, id); }
const MessageDescriptor &serverMessage(uint8_t id) { return find(servers, id); }
std::string_view domainName(MessageDomain domain) {
    switch (domain) {
#define DOMAIN(name) case MessageDomain::name: return #name;
    DOMAIN(Lifecycle) DOMAIN(Movement) DOMAIN(Combat) DOMAIN(Inventory) DOMAIN(Interaction)
    DOMAIN(Progression) DOMAIN(Social) DOMAIN(World) DOMAIN(Character)
#undef DOMAIN
    }
    throw std::logic_error("Invalid message domain");
}
std::string_view supportName(MessageSupport support) {
    switch (support) {
    case MessageSupport::Stub: return "stub";
    case MessageSupport::AdmissionOnly: return "admission-only";
    case MessageSupport::Implemented: return "implemented";
    }
    throw std::logic_error("Invalid message support");
}
size_t clientPacketSize(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return 0;
    const auto &entry = clientMessage(bytes[0]);
    if (entry.fixedSize) return bytes.size() >= entry.fixedSize ? entry.fixedSize : 0;
    if (entry.id != uint8_t(ClientMessage::Chat)) throw ProtocolError("Missing native packet framer");
    if (bytes.size() < 3) return 0;
    size_t at = 3;
    for (int field = 0; field < 2; ++field) {
        const auto start = at;
        while (at < bytes.size() && bytes[at]) ++at;
        if (at - start > (field == 0 ? 255u : 15u)) throw ProtocolError("Chat string exceeds native limit");
        if (at == bytes.size()) return 0;
        ++at;
    }
    if (at == bytes.size()) return 0;
    const auto length = at + 1 + bytes[at];
    return bytes.size() >= length ? length : 0;
}
void requirePhase(const MessageDescriptor &entry, GamePhase phase) {
    const bool valid = phase != GamePhase::Closed && (entry.phase == GamePhase::Connected
        ? phase == GamePhase::Connected : entry.phase == GamePhase::LoggedOn
        ? phase == GamePhase::LoggedOn || phase == GamePhase::Entered : phase == GamePhase::Entered);
    if (!valid) throw ProtocolError("Packet is unavailable in this game phase: " + std::string(entry.name));
}
void validateServerPacket(std::span<const uint8_t> bytes) {
    if (bytes.empty()) throw ProtocolError("Empty server packet");
    const auto &entry = serverMessage(bytes[0]);
    if (entry.support == MessageSupport::Stub)
        throw ProtocolError("Server serializer is not implemented: " + std::string(entry.name));
    if (net::protocol::lod113c_packet_size(bytes) != bytes.size())
        throw ProtocolError("Invalid outgoing native packet size: " + std::string(entry.name));
}
}
