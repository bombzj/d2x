#pragma once
#include "gameplay/combat/damage_type.hpp"
#include "damage_data.hpp"
#include "gameplay/effects/definition.hpp"
#include "world/collision.hpp"
#include "gameplay/skills/damage_curve.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <array>

namespace d2x {
struct SkillFormulaCatalog;
struct MonsterTypeCatalog;
struct MissileLevelDamage {
    int skill{-1},hitShift{},maximumDamage{},length{};
    SkillDamageCurve minimum,maximum;
    std::array<int,3> lengthPerLevel{};
};
struct MissileSkillDamage {int rankSkill{-1},damageSkill{-1};};
struct MissilePathInitialization {
    enum class Kind { None,ChargedSkill,ChargedBurst,DiabWall } kind{};
    int index{};
};
struct MissileRule {
    enum class Behavior { Projectile, Fireball, FireHead, SpiderLay, SpiderGoo, FirewallMaker, Fire, Charged, ColdNova, PoisonCloud, GroundThrow, GlacialSpike, Meteor, PoisonBottle } behavior{Behavior::Projectile};
    int definition{-1}, frames{}, sourceDamage{}, minimum{}, maximum{}, elementalMinimum{}, elementalMaximum{};
    int nextDelay{}, hitClass{};
    uint64_t coldFrames{}, poisonFrames{};
    DamageType element{DamageType::Physical};
    float speed{};
    MissileCollisionRule collision;
    bool killOnHit{}, clientSend{}, returnFire{}, toHit{};
    bool town{},sourceTown{},pierce{},collideFriend{},lastCollide{},explosion{};
    uint8_t collisionMode{};
    int baseVelocity{}, levelVelocity{}, rank{1}, activate{};
    float blastRadius{};
    bool canSlow{}, collidePlayers{true}, collideMonsters{true}, alwaysExplode{};
    bool noMultiShot{}, noUniqueMod{}, unspreadMultiShot{};
    float acceleration{}, maximumVelocity{};
    std::shared_ptr<const MissileRule> cloud;
    int cloudMainStep{},cloudSubStep{};
    int chainCount{},chainRadius{};
    float cloudMainSpeed{},cloudSubSpeed{};
    bool freeze{};
    int childStep{1};
    std::shared_ptr<const MissileRule> impactMissile;
    std::optional<MissileLevelDamage> levelDamage;
    std::optional<MissileSkillDamage> skillDamage;
    int originSourceDamage{},linkedSkill{-1};
    uint8_t holy{};
    bool useMissileSkill{},halfTwoHanded{},applyMastery{};
    MissileDamageElement damageElement{};
    std::shared_ptr<const StatRuleCatalog> damageStats;
    std::shared_ptr<const SkillFormulaCatalog> damageFormulas;
    std::shared_ptr<const MonsterTypeCatalog> damageTypes;
    int lifeStealOverlay{-1},manaStealOverlay{-1};
    std::array<int,3> lifeStealDivisor{1,1,1},manaStealDivisor{1,1,1};
    std::array<int,3> aiCurseDivisor{1,1,1};
    bool nextHit{};
    CombatStateDefinition nextHitState;
    int32_t elementLength{};
    uint16_t serverDamageFunction{},serverDoFunction{},serverHitFunction{};
    std::array<int32_t,5> serverParameters{};
    std::array<int32_t,3> hitParameters{};
    std::array<int,3> subMissiles{-1,-1,-1};
    std::array<int,4> hitSubMissiles{-1,-1,-1,-1};
    bool canDestroy{};
    std::array<int32_t,2> damageParameters{};
    int programOverlay{-1};
    bool getHit{},softHit{};
    uint8_t knockbackChance{};
    int uninterruptableState{-1};
};
}
