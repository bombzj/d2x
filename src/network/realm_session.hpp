#pragma once
#include "contracts/online.hpp"
#include "network/protocol/auth.hpp"
#include "network/protocol/wire.hpp"
#include "network/tcp_stream.hpp"
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
// Headless account/Realm/game coordinator. All calls, including tick(), are made
// on the owning thread. read() is borrowed until the next mutating call.
// No GameSession, local D2S, MPQ, graphics, or operating-system API is required.
class RealmSession {
  public:
    RealmSession();
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
    bool create_game(CreateGameOptions options);
    bool join_game(std::string name, std::string password = {});
    bool leave_game();
    // Raw original movement request. Application must first validate its current
    // MPQ scene binding; neither this method nor the replica predicts a position.
    bool move_to(OnlinePoint target, bool run = true);
    // Uses an assigned server UNIT_TILE ID. Map warp classes/slots are not IDs.
    bool use_exit(uint32_t serverUnitId);
    bool interact_map_unit(OnlineUnitKey target);
    // Caller maps the destination's Levels.Waypoint index from current MPQ.
    // Zero destination closes the server-opened menu. Never unlocks locally.
    bool use_waypoint(uint16_t destination, uint8_t waypointNumber = 0);
    bool return_to_characters();
    void tick();
    void cancel();
    void logout();
    const OnlineView &read() const;
    // Opaque, bounded, ordered packets retained for additional world consumers.
    // The basic replica in read().world has already consumed supported messages.
    // Taking packets does not mutate the view revision. No auth or tickets are exposed.
    std::vector<GamePacket> take_game_packets();

  private:
    void authenticate(LoginOptions options, bool createAccount);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace d2x::net
