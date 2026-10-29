#pragma once
#include "core/id.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/character/hireling_record.hpp"
#include "server/game_commands.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "core/math.hpp"
#include "world/identity.hpp"
#include "world/interaction_geometry.hpp"
#include <cstdint>
#include <array>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
// Internal authority/host values. These are not client messages or wire structs.
struct PlayerSnapshot {
    struct Hover { std::string text; uint64_t revision{}; std::set<PlayerId> recipients; uint8_t language{}; };
    std::optional<Hover> hover;
    uint16_t partyId{UINT16_MAX};
    struct SocialRelation {uint8_t partyStatus{};uint16_t flags{};};
    std::map<PlayerId,SocialRelation> socialRelations;
    GameHandle game;
    PlayerId recipient;
    uint64_t tick{}, revision{}, areaGeneration{};
    struct Motion {
        EntityId id;
        RegionId region{};
        Vec position, look, nextPosition;
        bool moving{}, running{};
        float movementSpeed{};
        std::optional<MotionTarget> target{};
    } actor;
    // Coalesced diagnostic result, not a reliable transaction acknowledgement.
    CharacterAttributes attributes;
    EquipmentStats equipment;
    std::map<int, int> skillRanks;
    std::map<int,int> itemSkills;
    std::set<int> states;
    std::map<int,std::vector<StatValue>> stateStats;
    Vec knockbackSource;
    CommandResult command;
    uint64_t movementSequence{}, inventoryRevision{}, characterRevision{};
    std::string name, characterClass;
    int level{};
    bool entered{};
    bool paused{};
    Fixed8 life,mana,stamina;
    uint8_t potionLifePercent{}, potionManaPercent{};
    bool attacking{}, recovering{}, deadSettled{};
};
struct PersistentCharacter;
struct PetOwnershipSnapshot {
    EntityId id, owner;
    uint8_t type{};
    uint16_t nativeClass{};
    uint16_t lifePercentage{};
    RegionId area;
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
    std::map<int,std::vector<StatValue>> stateStats{};
    std::shared_ptr<const PersistentCharacter> equipment{};
    int appearOverlay{-1};
    std::optional<EntityId> storedOwner{};
    std::optional<MotionTarget> movementTarget{};
    std::optional<uint8_t> poseDirection{};
    uint8_t pathSteps{};
    uint8_t pathType{13}, pathDistance{5}; // MonsterMode's ordinary TowardFinish/default distance.
    uint64_t movementRevision{};
    bool rising{};
    bool hostile{};
    std::vector<uint8_t> modifiers{};
    std::array<uint8_t,16> components{}, componentCounts{};
    uint8_t rankFlags{};
    uint16_t nameSeed{}, superUniqueIndex{};
    bool lightningReady{};
    std::optional<HirelingRecord> hireling{};
    EntityId hirelingOwner{};
    int64_t hirelingLife{};
    std::map<std::string,int64_t,std::less<>> hirelingAttributes{};
    std::vector<StatValue> assignmentStats{}; // First sixteen base entries of the monster's MonProp list.
};
} // namespace d2x
