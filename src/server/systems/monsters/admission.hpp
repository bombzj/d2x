#pragma once
#include "server/runtime/contracts.hpp"
#include "core/random.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include <optional>
#include <memory>
#include <set>
#include <vector>

namespace d2x { struct PersistentCharacter; }
namespace d2x::server::monsters {
struct Actor;
struct ModifierPreparation {
    struct Unit {EntityId id;uint64_t revision{};MonsterIdentity identity;int level{},nativeClass{};std::vector<int> modifiers{};};
    uint64_t token{},tick{},generation{},expectedRandom{},nextRandom{};
    RegionId area;Unit leader;MonsterEnchantment selection;std::vector<Unit> minions;
    bool promoteMinions{};
    std::optional<uint16_t> initialNameSeed{}; // Restore constructs before overwriting saved name.
};
struct PreparedModifiers {
    uint64_t token{};MonsterRule leader;std::map<EntityId,MonsterRule> minions;std::string deferred;
    std::optional<AuraDefinition> initialAura{};
};
struct EquipmentPreparation {
    uint64_t token{},tick{},generation{},gameRandom{},unitRandom{};
    EntityId actor;uint64_t itemCursor{};RegionId area;MonsterRule rule;
    int strength{},dexterity{},defense{},rating{};
    std::set<size_t> uniques,uniquesExpected;
};
struct PreparedEquipment {
    uint64_t token{},gameRandom{},unitRandom{};size_t allocatedItems{};
    std::shared_ptr<const PersistentCharacter> equipment;
    std::vector<std::shared_ptr<const StatList>> sources;
    std::optional<WeaponDamage> weapon;std::optional<EquipmentStats> stats;
    std::set<size_t> limitedUniques;
    std::string deferred;
};
// Shared native MonEquip row selection; item construction remains in hosting.
DomainResult<std::vector<MonsterRule::BirthEquipment::Choice>> selectBirthEquipment(const MonsterRule &rule,uint64_t &random);
struct Admission {
    MonsterIdentity identity;
    std::optional<MonsterKind> implementation;
    RegionId area;
    Vec position;
    bool hostile{};
    std::optional<MonsterRule> rule;
    std::vector<Vec> skillPositions{};
    uint64_t tick{};
    bool deadMode{};
    EntityId minionOwner{};
    bool ownsMinions{};
    bool transferMinions{};
    bool wanderTarget{};
    bool resurrected{};
    int birthTicks{};
    bool ignoreSpawnCollision{};
    bool uncounted{};
    // MONSTER_Reinitialize retains pUnit->pSeed instead of allocating a seed.
    std::optional<uint64_t> unitRandom{};
    int pd2SceneLifePercent{};
};
}
