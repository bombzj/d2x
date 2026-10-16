#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/combat_rules.hpp"
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include <optional>
#include <vector>

namespace d2x::server::monsters {
struct Actor;
struct ModifierPreparation {
    struct Unit {EntityId id;uint64_t revision{};MonsterIdentity identity;int level{},nativeClass{};std::vector<int> modifiers;};
    uint64_t token{},tick{},generation{},expectedRandom{},nextRandom{};
    RegionId area;Unit leader;MonsterEnchantment selection;std::vector<Unit> minions;
    bool promoteMinions{};
};
struct PreparedModifiers {
    uint64_t token{};MonsterRule leader;std::map<EntityId,MonsterRule> minions;std::string deferred;
};
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
    EntityId minionOwner;
    bool ownsMinions{};
    bool transferMinions{};
    bool wanderTarget{};
};
}