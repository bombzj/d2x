#pragma once
#include "gameplay/skills/rule_spec.hpp"
#include "gameplay/actions/shape_profile.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/hydra_spec.hpp"
#include "gameplay/skills/cast_timing.hpp"
#include "gameplay/skills/aura_resolve.hpp"
#include "world/collision.hpp"
#include "combat_rules.hpp"
#include "gameplay/combat/attack_timing.hpp"
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
struct HydraHeadRules {
    HydraHeadSpec definition;
    std::array<MonsterRule,3> rules;
    std::array<UnitCombatStats,3> base;
    bool noRatio{},boss{};
};
struct HydraRules {
    std::array<HydraHeadRules,3> heads;
    std::map<int,std::array<int,3>> areaLevels;
    std::vector<std::array<int,6>> defenseRatios;
};
struct SkillRules {
    bool pd2{};
    std::shared_ptr<const StatRuleCatalog> stats;
    std::shared_ptr<const SkillFormulaCatalog> formulas;
    struct Aura { AuraSkillSpec spec; bool immediate{}; };
    std::map<int,Aura> auras;
    std::map<std::string,int,std::less<>> nativeStats;
    CombatStateDefinition redeemed,holyShield,alignment,stunned,syncWarped,shatter,terror;
    CombatStateDefinition chilled,poisoned,openWounds,frozen,justHit,uninterruptable,burning,fireShrine;
    CombatStateDefinition healthPotion,manaPotion,itemFullSet;
    std::shared_ptr<const MonsterTypeCatalog> monsterTypes;
    int poisonState{-1};
    std::set<int> dismissibleSummons;
    std::map<int,int> petTypeBaseLimits,skillPetTypes;
    std::set<int> warpPetTypes;
    std::set<int> rangePetTypes;
    std::map<int,MonsterRule> amazonPetRules;
    std::set<int> slowableMissiles, pierceableMissiles, alwaysExplodingMissiles;
    std::map<int,std::pair<int,int>> missileVelocities;
    std::optional<HydraRules> hydra;
    std::map<int,HydraRules> hydraPrograms;
    const HydraRules *hydraProgram(int skill) const {
        const auto found=hydraPrograms.find(skill);
        return found!=hydraPrograms.end()?&found->second:hydra?&*hydra:nullptr;
    }
    std::map<int, SkillDefinition> definitions;
    std::map<int,ShapeProfile> shapes;
    std::map<int,SkillShapeRestriction> shapeRestrictions;
    std::map<std::string, CastAnimationTiming, std::less<>> animations;
    std::map<std::string, AttackAnimation, std::less<>> weaponAnimations;
    std::map<std::string, AnimationTiming, std::less<>> recoveryAnimations;
    std::map<int, MissileCollisionRule> collisions;
    std::set<std::string,std::less<>> normalWeaponCodes;
    std::map<int, bool> returnFire, clientSend;
    std::array<int, 3> staticMinimum{}, coldDivisor{}, freezeDivisor{};
    std::array<int,3> aiCurseDivisor{1,1,1};
    std::array<int,3> lifeStealDivisor{1,1,1}, manaStealDivisor{1,1,1};
    int crushingOverlay{-1}, lifeStealOverlay{-1}, manaStealOverlay{-1};
};
}
