#pragma once
#include "hosting/character_store.hpp"
#include "native_realm_host.hpp"
#include "hosting/game_host.hpp"
#include "hosting/game_content.hpp"
#include "hosting/administration.hpp"
#include "hosting/protocol/message_catalog.hpp"
#include "content/classic_data.hpp"
#include "content/character/realm_portrait.hpp"
#include <array>
#include <functional>

namespace d2x::hosting {
// One peer protocol/session binding, independent of byte transport and UI.
// Protocol handlers orchestrate storage/admission; gameplay goes to domain handlers.
struct NativeRealmService {
    using RealmOutput = std::function<void(uint8_t, Bytes)>;
    using GameOutput = std::function<void(Bytes)>;
    NativeRealmHost &shared;
    Archives &archives;
    std::filesystem::path &root;
    RealmOutput realmOutput;
    GameOutput gameOutput;
    std::shared_ptr<const ClassicData> &content;
    std::unique_ptr<RealmPortraitCatalog> &portraits;
    std::unique_ptr<CharacterStore> store;
    GameHost &host;
    uint64_t &rules;
    bool authenticated{}, ticket{};
    bool multiplayerEndpoint{};
    uint32_t hash{};
    uint16_t token{};
    // TCP peers receive the interface reached by their MCP connection; memory
    // peers keep loopback. No host-wide address leaks between transports.
    std::array<uint8_t, 4> gameAddress{127, 0, 0, 1};
    unsigned gameDifficulty{};
    std::string gameName, gamePassword, selectedName, failure, startupFile;
    CharacterRosterView roster;
    std::unique_ptr<CharacterStore::Lease> lease;
    std::optional<PersistentCharacter> selected;
    std::optional<PlayerBinding> binding;
    GeneratedArea terrain;
    std::vector<Bytes> admission;
    struct GamePeer {
        GamePhase phase = GamePhase::Closed;
        uint64_t sequence{}, sentRevision{}, sentMovement{};
        Bytes lastMotion;
        std::set<RegionId> areas;
        std::map<EntityId, std::pair<uint64_t, std::string>> roster;
        struct VisiblePlayer { EntityId actor; uint64_t inventoryRevision{}; Bytes motion; std::set<EntityId> equipment; };
        std::map<PlayerId, VisiblePlayer> visible;
        struct VisibleMonster { Bytes motion; };
        std::map<EntityId, VisibleMonster> monsters;
        std::map<EntityId, uint64_t> groundItems, corpses, objects, portals;
        std::map<EntityId, std::set<EntityId>> corpseEquipment;
        std::map<int,int> itemSkills;
        std::set<EntityId> npcs, shopItems;
        EntityId shopOwner;
        std::map<EntityId, std::set<int>> states;
    } peer;
    struct PreparedGame {
        PlayerBinding binding;
        GeneratedArea terrain;
        std::vector<Bytes> admission;
    };
    struct ReloadCandidate {
        PreparedGame game;
        Bytes expectedFile;
        std::string name;
        unsigned difficulty{};
    };
    std::optional<ReloadCandidate> reload;
    HostDiagnostics counters;

    NativeRealmService(NativeRealmHost &, RealmOutput, GameOutput);
    ~NativeRealmService();
    void receiveEvent(const server::EventBatch &);
    void publishPlayers();
    void publishMonsters();
    void publishGroundItems();
    void publishCorpses();
    void publishObjects();
    void publishNpcs();
    void publishShop();
    void publishPortals();
    void publishItemSkills();
    void publishAreas(RegionId area);
    void changeArea(const server::TravelFact &);
    void initialize();
    void sendRealm(uint8_t, net::protocol::Writer);
    void sendGame(Bytes);
    void sendGameBatch(std::vector<Bytes>);
    void result(uint8_t, uint32_t);
    const CharacterRosterEntry &find(std::string_view) const;
    void checkpoint();
    void prepareReload();
    PreparedGame prepareGame(PersistentCharacter, bool singlePlayer = true);
    void discardReload();
    void close(bool save = true, bool keepReload = false);
    void resetRealm();
    void connectGame(bool announce = true);
    void failGame();
    void setPaused(bool);
    void publishMotion(bool force = false);
    void publishEvents();
    HostDiagnostics diagnostics() const;
    AdminResult administer(const AdminRequest &);
    std::string prepareStartup(const std::string &, const std::string &, const std::string &);
    void realm(const net::protocol::Packet &);
    void game(std::span<const uint8_t>);
    // MCP entry points: no gameplay implementations belong in these methods.
    void startup(net::protocol::Reader &);
    void listCharacters(net::protocol::Reader &);
    void createCharacter(net::protocol::Reader &);
    void deleteCharacter(net::protocol::Reader &);
    void selectCharacter(net::protocol::Reader &);
    void createGame(net::protocol::Reader &);
    void joinGame(net::protocol::Reader &);
    void listGames(net::protocol::Reader &);
    void gameInfo(net::protocol::Reader &);
    // Native game lifecycle; domain requests live under protocol/handlers_*.
    void logon(net::protocol::Reader &);
    void enterEnvironment(net::protocol::Reader &);
    void saveAndLeave(net::protocol::Reader &);
    void ping(net::protocol::Reader &);
};
}
