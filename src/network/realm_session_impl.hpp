#pragma once
// Private implementation of RealmSession; included only by realm_session_*.cpp.
#include "realm_session.hpp"
#include "network/chat_command.hpp"
#include "network/tcp_stream.hpp"
#include "client/remote_world.hpp"
#include "network/protocol/d2gs_stream.hpp"
#include "network/protocol/message_schema.hpp"
#include "network/protocol/bits.hpp"
#include "network/protocol/tcpip.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>
#include <set>
#include <chrono>

namespace d2x {
namespace net {
using namespace protocol;
using Clock = std::chrono::steady_clock;
namespace realm_session_impl_detail {
inline Bytes preauthenticatedRealmStartup() {
    Writer out;
    out.append(Bytes(64, 0)); // Cookie, status, native 8-byte and 48-byte chunks.
    out.string("SinglePlayer", 64);
    return out.release();
}
inline bool text_valid(std::string_view value, size_t maximum, bool allowEmpty = false) {
    return (allowEmpty || !value.empty()) && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 32 && c < 127; });
}
inline bool character_name_valid(std::string_view name) {
    const auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    return !name.empty() && name.size() <= 15 &&
           std::all_of(name.begin(), name.end(),
                       [&](unsigned char c) { return letter(c) || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}
inline std::string ip_address(Reader &in) {
    std::string host;
    for (size_t i = 0; i < 4; ++i) {
        if (i)
            host += '.';
        host += std::to_string(in.u8());
    }
    return host;
}
inline void finish_zero_padding(Reader &in) {
    while (in.remaining())
        if (in.u8())
            throw ProtocolError("Unexpected packet trailing bytes");
}
inline OnlineCharacter character_record(Reader &in) {
    OnlineCharacter result;
    result.expiration = in.u32();
    result.name = in.string(15);
    auto portrait = in.string(64);
    result.portrait.assign(portrait.begin(), portrait.end());
    // PvPGN's unsaved creation preview is version 4. Saved native 1.13c
    // previews are version 13 (8D 80), with 14-bit encoded client flags.
    // D2MOO Clients.cpp describes the native layout; do not read a guild
    // emblem byte as Ladder status in the saved layout.
    const auto &bytes = result.portrait;
    const auto version = bytes.size() == 33 ? uint16_t((bytes[0] & 0x7F) | ((bytes[1] & 0x7F) << 7)) : 0;
    if (bytes.size() == 33 && (bytes[0] & 0x80) && (bytes[1] & 0x80) &&
        (version == 4 || version == 10 || version == 13) && bytes[13] >= 1 && bytes[13] <= 7 &&
        bytes[25] >= 1 && bytes[25] <= 99) {
        result.characterClass = uint8_t(result.portrait[13] - 1);
        result.level = result.portrait[25];
        const auto status = uint16_t((bytes[26] & 0x7F) | ((bytes[27] & 0x7F) << 7));
        result.expansion = (status & 0x20) != 0;
        result.hardcore = (status & 0x04) != 0;
        result.dead = (status & 0x08) != 0;
        if (version != 4)
            result.progression = uint8_t((status >> 8) & 0x1F);
        result.ladder = version == 4 ? bytes[30] != 0 && bytes[30] != 0xFF : (status & 0x40) != 0;
    }
    return result;
}
inline bool pending_stage(OnlineStage stage) {
    switch (stage) {
    case OnlineStage::Idle:
    case OnlineStage::RealmSelection:
    case OnlineStage::CharacterSelection:
    case OnlineStage::Lobby:
    case OnlineStage::ProtocolReady:
    case OnlineStage::Failed:
    case OnlineStage::Cancelled:
        return false;
    default:
        return true;
    }
}
} // namespace realm_session_impl_detail
using namespace realm_session_impl_detail;
struct RealmSession::Impl {
    // Calls and background service are serialized. Nested native operations (for
    // example Hardcore respawn -> leave) reuse this same lock.
    std::recursive_mutex mutex;
    std::condition_variable_any wake;
    bool retainGamePackets{}, snapshotDirty{};
    std::jthread worker;

    ByteStream sid, mcp, gs;
    std::optional<Endpoint> directRealm;
    bool directTcpIp{}, tcpIpUploaded{}, tcpIpSavedOnLeave{};
    std::optional<uint16_t> tcpIpGameId;
    Bytes tcpIpUpload;
    TcpIpSaveTransfer tcpIpDownload;
    std::vector<Bytes> tcpIpSaves;
    PacketStream sidPackets{Framing::Sid}, mcpPackets{Framing::Mcp};
    D2gsStream gamePackets;
    OnlineView view;
    LoginOptions options;
    Digest accountHash{}, loginHash{};
    uint32_t clientToken{}, serverToken{}, ticketHash{};
    uint16_t requestCounter{}, pendingRequest{}, lastListRequest{}, ticketToken{};
    uint16_t gameInfoRequest{};
    Clock::time_point gameInfoDeadline{};
    std::optional<OnlineCharacter> selected;
    std::string pendingCharacter, gameName, gamePassword;
    std::vector<GamePacket> worldPackets;
    std::function<void(uint64_t, const Packet &)> gamePacketObserver;
    std::function<void(uint64_t, std::span<const uint8_t>)> gameRequestObserver;
    size_t worldBytes{};
    Clock::time_point deadline{}, gameStarted{}, nextHeartbeat{}, lastPing{}, nextMovement{};
    std::optional<uint32_t> initializedNpc;
    Clock::time_point npcDeadline{}, waypointDeadline{}, playerTradeDeadline{};
    struct PortalRequest {
        std::optional<OnlineSkillSelection> previous;
        std::set<uint32_t> existing;
        Clock::time_point deadline;
    };
    std::optional<PortalRequest> portalRequest;
    Clock::time_point itemDeadline{}, nextItem{}, storageDeadline{};
    uint64_t itemSequence{}, errorSequence{};
    Clock::time_point combatDeadline{}, nextCombat{};
    Clock::time_point respawnDeadline{};
    uint64_t combatSequence{};
    bool waitingChat{}, environmentSent{}, awaitingPong{}, registering{};
    std::optional<Endpoint> gameEndpoint;
    std::string listFilter;

    Bytes realmStartup;

    explicit Impl(bool retain);
    ~Impl();
    void service();
    void start();
    void changed();
    void record_combat(OnlineCombatRequest request);
    void stage(OnlineStage value);
    void clear_credentials();
    void clear_game();
    void shutdown();
    void error(OnlineErrorKind kind, std::string message, uint8_t id = 0, uint32_t code = 0);
    void fail(OnlineErrorKind kind, std::string message, uint8_t id = 0, uint32_t code = 0);
    bool require(OnlineStage value);
    bool current_game(const OnlineView &published) const;
    bool require_game(const OnlineView &published);
    bool require_intent(const OnlineIntentContext &context, bool interaction);
    bool require_navigation(const OnlineIntentContext &context);
    void finish_pickup(OnlineItemRequest::State state);
    void observed(OnlineProtocolCounters &counters, const Packet &packet, size_t header);
    void sent(ByteStream &stream, Bytes bytes);
    void send_sid(uint8_t id, Writer out = {});
    void send_mcp(uint8_t id, Writer out = {});
    uint16_t next_request_id();
    uint16_t request_id();
    void expect(OnlineStage value) const;
    void request_realms();
    void request_logon();
    void request_characters();
    void start_account();
    void handle_sid(Packet packet);
    void join_requested_game();
    void handle_mcp(Packet packet);
    void game_rejected(uint8_t id, uint32_t code, std::string message);
    bool choose_realm(std::string name);
    void return_to_realm();
    void reset_area(bool preserveInitialPosition);
    void handle_game(Packet packet);
    void pump_sid();
    void pump_mcp();
    void pump_game();
    void close_interaction();
    void restore_portal_skill();
    void tick();
};
} // namespace net
} // namespace d2x
