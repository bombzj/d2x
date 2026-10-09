#pragma once
#include "contracts/online.hpp"
#include "network/protocol/auth.hpp"
#include "network/protocol/wire.hpp"
#include "network/byte_transport.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace d2x::net {
struct LoginOptions {
    Endpoint accountServer;
    std::string account, password;
    protocol::OriginalClientAuth originalClient;
    uint32_t localeId{1033}, languageCode{0x656E5553}; // enUS tag in the protocol's DWORD order.
    int32_t timeZoneBiasMinutes{};
    std::string countryCode{"USA"}, countryName{"United States"};
    std::chrono::milliseconds timeout{15000};
    std::chrono::milliseconds heartbeat{5000};
    uint16_t gamePort{4000};
    uint8_t gameLocale{};
};
struct CreateGameOptions {
    std::string name, password, description;
    uint8_t difficulty{}, maximumPlayers{8}, levelDifference{99};
};
struct CreateCharacterOptions {
    std::string name;
    uint8_t characterClass{};
    bool hardcore{}; // LoD, non-Ladder only. Server initializes all character data.
};
struct GamePacket {
    uint64_t gameGeneration{};
    protocol::Packet packet;
};
// Headless account/Realm/game coordinator. A private worker services transport,
// heartbeats and the authoritative replica independently of window/loading waits.
// Public calls stay on the owning client thread. read() borrows its snapshot until
// tick() or a mutating command; the worker never modifies that snapshot.
// No GameSession, local D2S, MPQ, graphics, or operating-system API is required.
class RealmSession {
  public:
    explicit RealmSession(bool retainGamePackets = false);
    // Preauthenticated Realm admission (embedded host today). Native MCP/D2GS
    // codecs, state reducer and all public gameplay commands remain unchanged.
    void connect_realm(std::unique_ptr<IByteTransport> mcp, std::unique_ptr<IByteTransport> game,
                       Endpoint realm, std::string name, uint16_t gamePort = 4000);
    ~RealmSession();
    RealmSession(const RealmSession &) = delete;
    RealmSession &operator=(const RealmSession &) = delete;
    void login(LoginOptions options);
    void register_account(LoginOptions options);
    bool choose_realm(std::string name);
    bool select_character(std::string name);
    bool create_character(CreateCharacterOptions options);
    bool delete_character(std::string name);
    bool return_to_realms();
    bool cancel_game_list();
    bool list_games(std::string filter = {});
    bool query_game(std::string name);
    bool create_game(CreateGameOptions options);
    bool join_game(std::string name, std::string password = {});
    // Cancel pre-entry requests by retiring MCP; after logon, wait for native save/leave.
    bool leave_game();
    // Normal D2GS room broadcast. Never inserts a local echo; read().world.social
    // receives only original server messages. No scene/life/interaction gate:
    // chat also remains available while dead, in a panel, or changing areas.
    bool send_chat(std::string text);
    // Respond only to the current server invitation. Accept is TRADEBTN_PERFORM
    // (3), not the final item-exchange acceptance (4). No local trade execution.
    bool respond_player_trade(bool accept, uint64_t revision, std::optional<OnlineIntentContext> context = {});
    bool update_player_trade(OnlinePlayerTradeAction, uint64_t revision, uint32_t amount = 0,
                             std::optional<OnlineIntentContext> context = {});
    bool resurrect(std::optional<OnlineIntentContext> context = {}); // Original 0x41, after server PLRMODE_DEAD.
    // Raw original movement request. Application must first validate its current
    // MPQ scene binding; neither this method nor the replica predicts a position.
    bool move_to(OnlinePoint target, bool run = true, std::optional<OnlineIntentContext> context = {});
    bool move_to_unit(OnlineUnitKey target, bool run = true, std::optional<OnlineIntentContext> context = {});
    // Uses an assigned server UNIT_TILE ID. Map warp classes/slots are not IDs.
    bool use_exit(uint32_t serverUnitId);
    bool interact_map_unit(OnlineUnitKey target, OnlineObjectIntent intent = OnlineObjectIntent::Operate,
                           std::optional<OnlineIntentContext> context = {});
    // Application validates MonStats.interact and native approach range first.
    bool interact_npc(uint32_t serverUnitId, std::optional<OnlineIntentContext> context = {});
    bool close_npc(std::optional<OnlineIntentContext> context = {});
    bool acknowledge_npc_message(uint16_t stringId, std::optional<OnlineIntentContext> context = {});
    bool npc_travel(uint32_t parameter, std::optional<OnlineIntentContext> context = {}); // Original action 0, NPC-dependent parameter.
    bool hireling_service(OnlineHirelingAction, std::optional<uint16_t> name = {}, std::optional<OnlineIntentContext> context = {});
    bool create_town_portal(uint16_t skillId, std::optional<OnlineIntentContext> context = {}); // MPQ-resolved item skill.
    // MPQ consumer checks the native location, ownership, shape and operation eligibility.
    // No optimistic inventory changes; the original protocol has no generic transaction ACK.
    bool submit_item(OnlineItemCommand command);
    bool submit_staff(uint32_t source,std::optional<uint32_t> item,std::optional<OnlineIntentContext> context = {});
    // Caller validates current MPQ skill and target eligibility. Cast/stop have
    // no generic native ACK; damage, resources and skills remain server-owned.
    bool submit_combat(OnlineCombatCommand command);
    // Caller maps the destination's Levels.Waypoint index from current MPQ.
    // Zero destination closes the server-opened menu. Never unlocks locally.
    bool use_waypoint(uint16_t destination, uint8_t waypointNumber = 0, std::optional<OnlineIntentContext> context = {});
    bool return_to_characters();
    void tick();
    void cancel();
    void logout();
    const OnlineView &read() const;
    std::chrono::milliseconds request_timeout() const;
    bool item_request_ready() const;
    // Optional (constructor opt-in), bounded, ordered packets for additional consumers.
    // The basic replica in read().world has already consumed supported messages.
    // Taking packets does not mutate the view revision. No auth or tickets are exposed.
    std::vector<GamePacket> take_game_packets();

  private:
    void authenticate(LoginOptions options, bool createAccount);
    struct Impl;
    mutable OnlineView snapshot_;
    std::unique_ptr<Impl> impl_;
};
} // namespace d2x::net
