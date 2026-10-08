#pragma once
#include "gameplay/monsters/identity.hpp"
#include "gameplay/monsters/kind.hpp"
#include "gameplay/monsters/ai_spec.hpp"
#include "world/navigation.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x::server {
// Value-only admission contracts; archive/table lookups belong to hosting.
struct AttackAnimation { int frames{}, speed{}, actionFrame{}, startFrame{}; };
struct MeleeRules { std::map<std::string, AttackAnimation, std::less<>> animations; };
struct MonsterRule {
    int nativeClass{}, level{}, minimumLife{}, maximumLife{}, defense{}, attackRating{};
    int minimumDamage{}, maximumDamage{}, criticalChance{};
    std::array<int, 6> resistances{};
    int size{}, meleeRange{}, attackTicks{}, impactTick{}, deathTicks{}, decisionTicks{};
    int nativeVelocity{}, difficulty{};
    MovementCollisionRule collision;
    bool demon{}, undead{};
    int coldEffect{}, coldState{-1}, frozenState{-1};
    int knockbackTicks{};
    std::vector<uint64_t> experience;
    MonsterAiProfile ai{};
};
struct PreparedMonster {
    MonsterIdentity identity;
    MonsterKind implementation{};
    Vec position;
    MonsterRule rule;
};
}
