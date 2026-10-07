#pragma once
#include "hosting/character_store.hpp"
#include "hosting/game_host.hpp"
#include "hosting/game_content.hpp"
#include "hosting/administration.hpp"
#include "hosting/protocol/message_catalog.hpp"
#include "content/classic_data.hpp"
#include "content/character/realm_portrait.hpp"
#include <functional>

namespace d2x::hosting {
// Single-participant hosting composition, independent of byte transport and UI.
// Protocol handlers orchestrate storage/admission; gameplay goes to domain handlers.
struct NativeRealmService {
    using RealmOutput = std::function<void(uint8_t, Bytes)>;
    using GameOutput = std::function<void(Bytes)>;
    Archives &archives;
    std::filesystem::path root;
    RealmOutput realmOutput;
    GameOutput gameOutput;
    std::unique_ptr<ClassicData> content;
    std::unique_ptr<RealmPortraitCatalog> portraits;
    std::unique_ptr<CharacterStore> store;
    GameHost host;
    uint64_t rules{};
    bool authenticated{}, ticket{};
    uint32_t hash{};
    uint16_t token{};
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

    NativeRealmService(Archives &, std::filesystem::path, RealmOutput, GameOutput);
    void initialize();
    void sendRealm(uint8_t, net::protocol::Writer);
    void sendGame(Bytes);
    void result(uint8_t, uint32_t);
    const CharacterRosterEntry &find(std::string_view) const;
    void checkpoint();
    void prepareReload();
    PreparedGame prepareGame(PersistentCharacter);
    void discardReload();
    void close(bool save = true, bool keepReload = false);
    void resetRealm();
    void connectGame();
    void failGame();
    void advance(double seconds, bool paused);
    void setPaused(bool);
    void publishMotion(bool force = false);
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
