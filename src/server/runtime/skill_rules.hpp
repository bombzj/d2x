#pragma once
#include "gameplay/skills/rule_spec.hpp"
#include "gameplay/skills/hydra_spec.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "world/collision.hpp"
#include "combat_rules.hpp"
#include <array>
#include <map>
#include <string>
#include <set>
namespace d2x::server {
struct SkillDefinition {
    SkillRuleSpec spec;
    bool allowedInTown{};
    MissileCollisionRule collision;
    bool itemTargetDo{};
    int itemEffect{},itemTarget{};
    bool itemCheckStart{},itemEffectUsesPreparedProgram{};
};
struct SkillRules {
    struct Aura { AuraSkillSpec spec; bool immediate{}; };
    std::map<int,Aura> auras;
    std::map<std::string,int,std::less<>> nativeStats;
    CombatStateDefinition redeemed,holyShield,alignment;
    int poisonState{-1};
    std::set<int> dismissibleSummons;
    std::map<int,MonsterRule> amazonPetRules;
    std::set<int> slowableMissiles, pierceableMissiles, alwaysExplodingMissiles;
    std::map<int,std::pair<int,int>> missileVelocities;
    std::optional<HydraSpec> hydra;
    std::map<int, SkillDefinition> definitions;
    std::map<std::string, CastAnimationTiming, std::less<>> animations;
    std::map<std::string, AttackAnimation, std::less<>> weaponAnimations;
    std::map<int, std::pair<int, int>> fireMasteries;
    std::map<int, std::pair<int, int>> lightningMasteries, coldMasteries;
    std::map<int, MissileCollisionRule> collisions;
    std::map<int, bool> returnFire, clientSend;
    std::array<int, 3> staticMinimum{}, coldDivisor{}, freezeDivisor{};
};
}
