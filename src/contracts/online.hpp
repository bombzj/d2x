#pragma once
#include "core/bytes.hpp"
#include "contracts/online_world.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
enum class OnlineStage {
    Idle,
    ConnectingAccount,
    AuthChallenge,
    AuthenticatingVersion,
    LoggingIn,
    ListingRealms,
    RealmSelection,
    ConnectingRealm,
    RealmStartup,
    ListingCharacters,
    CharacterSelection,
    SelectingCharacter,
    Lobby,
    ListingGames,
    CreatingGame,
    JoiningGame,
    ConnectingGame,
    GameHandshake,
    LoadingGame,
    ProtocolReady,
    LeavingGame,
    Failed,
    Cancelled,
    CreatingAccount,
    CreatingCharacter,
    DeletingCharacter
};
enum class OnlineErrorKind { Input, Transport, Protocol, Authentication, Server, Timeout, Unsupported };
struct OnlineError {
    OnlineErrorKind kind{};
    uint8_t packetId{};
    uint32_t serverCode{};
    std::string message;
    uint64_t sequence{}; // Stable across heartbeat/world revisions, local diagnostic identity.
};
struct OnlineRealm {
    std::string name, description;
};
struct OnlineCharacter {
    std::string name;
    uint32_t expiration{};
    Bytes portrait;
    std::optional<uint8_t> characterClass, level;
    std::optional<uint8_t> progression; // Native client-save progression bits; absent in legacy previews.
    std::optional<bool> expansion, hardcore, dead, ladder;
};
struct OnlineGame {
    std::string name, description;
    uint32_t index{}, flags{};
    uint8_t players{};
};
struct OnlineGameInfo {
    enum class State { Pending, Ready, TimedOut };
    State state{State::Pending};
    std::string name, description;
    uint32_t flags{}, uptimeSeconds{};
    uint8_t creatorLevel{}, levelDifference{}, maximumPlayers{};
    struct Player { std::string name; uint8_t characterClass{}, level{}; };
    std::vector<Player> players;
};
struct OnlineLobbyNotice {
    std::string text;
    bool error{};
};
struct OnlineLoadInfo {
    std::optional<uint8_t> act, difficulty;
    std::optional<uint16_t> townArea; // LOADACT metadata, not the player's current region.
    std::optional<uint32_t> mapSeed, secondarySeed, playerUnitId;
    bool serverLoadComplete{};
};
struct OnlineProtocolCounters {
    std::array<uint64_t, 256> received{}, sent{}, unconsumed{};
    uint64_t receivedBytes{}, sentBytes{};
    std::optional<uint8_t> lastReceived;
};
struct OnlineView {
    // Counts and IDs only: never packet payloads, credentials or native tickets.
    OnlineProtocolCounters sidProtocol, mcpProtocol, gameProtocol;

    uint64_t revision{}, connectionGeneration{}, gameGeneration{};
    OnlineStage stage{OnlineStage::Idle};
    std::optional<OnlineError> error;
    std::vector<OnlineRealm> realms;
    std::vector<OnlineCharacter> characters;
    std::vector<OnlineGame> games;
    std::optional<OnlineGameInfo> gameInfo;
    std::vector<OnlineLobbyNotice> lobbyNotices; // Bounded native SID info/error announcements, not chat.
    std::string selectedRealm, selectedCharacter;
    OnlineLoadInfo load;
    OnlineWorldView world;
    std::optional<uint32_t> latencyMilliseconds; // Unknown until the first native pong.
    std::optional<uint32_t> gameQueuePosition;
    bool gameListComplete{};

    void clear() {
        sidProtocol = {};
        mcpProtocol = {};
        gameProtocol = {};
        revision = connectionGeneration = gameGeneration = 0;
        stage = OnlineStage::Idle;
        error.reset();
        realms.clear();
        characters.clear();
        games.clear();
        gameInfo.reset();
        lobbyNotices.clear();
        selectedRealm.clear();
        selectedCharacter.clear();
        load = {};
        world.clear();
        latencyMilliseconds.reset();
        gameQueuePosition.reset();
        gameListComplete = false;
    }
};
inline OnlineIntentContext onlineIntentContext(const OnlineView &v) {
    return {v.connectionGeneration, v.gameGeneration, v.world.areaGeneration,
        v.world.interactionGeneration, v.load.playerUnitId, v.world.npcRequested};
}
inline bool onlineWorldMatches(const OnlineIntentContext &context, const OnlineView &v) {
    return context.connectionGeneration == v.connectionGeneration &&
        context.gameGeneration == v.gameGeneration && context.areaGeneration == v.world.areaGeneration &&
        context.player == v.load.playerUnitId;
}
inline bool onlineInteractionMatches(const OnlineIntentContext &context, const OnlineView &v) {
    return onlineWorldMatches(context, v) && context.interactionGeneration == v.world.interactionGeneration &&
        context.npc == v.world.npcRequested;
}
} // namespace d2x
