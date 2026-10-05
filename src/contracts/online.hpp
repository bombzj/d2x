#pragma once
#include "core/bytes.hpp"
#include "contracts/online_world.hpp"
#include <cstdint>
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
struct OnlineLoadInfo {
    std::optional<uint8_t> act, difficulty;
    std::optional<uint16_t> townArea; // LOADACT metadata, not the player's current region.
    std::optional<uint32_t> mapSeed, secondarySeed, playerUnitId;
    bool serverLoadComplete{};
};
struct OnlineView {
    uint64_t revision{}, connectionGeneration{}, gameGeneration{};
    OnlineStage stage{OnlineStage::Idle};
    std::optional<OnlineError> error;
    std::vector<OnlineRealm> realms;
    std::vector<OnlineCharacter> characters;
    std::vector<OnlineGame> games;
    std::string selectedRealm, selectedCharacter;
    OnlineLoadInfo load;
    OnlineWorldView world;
    uint32_t latencyMilliseconds{};
    std::optional<uint32_t> gameQueuePosition;
    bool gameListComplete{};
};
} // namespace d2x
