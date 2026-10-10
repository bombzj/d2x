#pragma once
#include "core/id.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/character/record.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include <compare>
#include <cstdint>
#include <array>
#include <map>
#include <set>
#include <memory>

namespace d2x {
// Internal authority/host values. These are not client messages or wire structs.
struct GameHandle {
    uint64_t slot{}, generation{};
    auto operator<=>(const GameHandle &) const = default;
};
struct PlayerId {
    uint64_t value{};
    auto operator<=>(const PlayerId &) const = default;
};
// Issued by the host. Transports bind this to a connection, never trust a
// player or game identity supplied in a command payload.
struct PlayerBinding {
    GameHandle game;
    PlayerId player;
};
enum class MovementAction { Move, Stop, ToggleRunning, ApproachExit, ApproachCorpse, ApproachObject, ApproachNpc };
struct MovementCommand {
    MovementAction action = MovementAction::Move;
    Vec destination;
    bool forceRun{};
    EntityId exit{};
};
enum class CommandStatus { Queued, Applied, NotImplemented, InvalidBinding, Stale, Paused, QueueFull, InvalidDestination, NoRoute, InvalidRequest, Unavailable, Conflict };
struct CommandResult {
    uint64_t sequence{};
    CommandStatus status = CommandStatus::Stale;
};
struct PlayerSnapshot {
    struct Hover { std::string text; uint64_t revision{}; std::set<PlayerId> recipients; };
    std::optional<Hover> hover;
    uint16_t partyId{UINT16_MAX};
    struct SocialRelation {uint8_t partyStatus{};uint16_t flags{};};
    std::map<PlayerId,SocialRelation> socialRelations;
    GameHandle game;
    PlayerId recipient;
    uint64_t tick{}, revision{}, areaGeneration{};
    uint64_t rulesFingerprint{};
    struct Motion {
        EntityId id;
        RegionId region{};
        Vec position, look, nextPosition;
        bool moving{}, running{};
        float movementSpeed{};
    } actor;
    // Coalesced diagnostic result, not a reliable transaction acknowledgement.
    CharacterAttributes attributes;
    EquipmentStats equipment;
    std::map<int, int> skillRanks;
    std::map<int,int> itemSkills;
    std::set<int> states;
    Vec knockbackSource;
    CommandResult command;
    uint64_t movementSequence{}, inventoryRevision{}, characterRevision{};
    std::string name, characterClass;
    int level{};
    bool entered{};
    bool paused{};
    float life{};
    bool attacking{}, deadSettled{};
};
struct PersistentCharacter;
struct PetOwnershipSnapshot {
    EntityId id, owner;
    uint8_t type{};
    uint16_t nativeClass{};
};
struct MonsterSnapshot {
    EntityId id;
    RegionId area;
    int nativeClass{};
    Vec position, destination;
    uint8_t life{}, mode{1};
    uint64_t revision{};
    bool moving{}, attacking{};
    int velocityPercent{75};
    bool running{};
    std::set<int> states{};
    Vec knockbackSource{};
    std::map<int,std::vector<std::pair<int,int64_t>>> stateStats{};
    std::shared_ptr<const PersistentCharacter> equipment{};
    int appearOverlay{-1};
    std::optional<EntityId> storedOwner{};
    std::vector<uint8_t> modifiers{};
    std::array<uint8_t,16> components{}, componentCounts{};
    uint8_t rankFlags{};
    uint16_t nameSeed{}, superUniqueIndex{};
    bool lightningReady{};
    std::optional<HirelingRecord> hireling{};
    EntityId hirelingOwner{};
    int64_t hirelingLife{};
    std::map<std::string,int64_t,std::less<>> hirelingAttributes{};
};
} // namespace d2x
