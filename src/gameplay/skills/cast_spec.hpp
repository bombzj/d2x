#pragma once
#include "core/fixed.hpp"
#include "gameplay/skills/behavior_fwd.hpp"
#include "gameplay/skills/curse_spec.hpp"
#include "gameplay/skills/elemental_spec.hpp"
#include "gameplay/skills/weapon_spec.hpp"
#include "gameplay/skills/summon_spec.hpp"
#include "gameplay/combat/missile_effects.hpp"
#include "gameplay/effects/spec.hpp"
#include "gameplay/items/skill_sources.hpp"
#include <optional>
#include <memory>

namespace d2x {
struct BoneSkillSpec;
struct AmazonMagicSpec;
struct CorpseSearchSpec;
struct LeapSpec;
struct GrimWardSpec;
struct AssassinSkillSpec;
struct SkillCastSpec {
    SkillBehavior effect{};
    std::shared_ptr<const BoneSkillSpec> bone;
    std::shared_ptr<const AmazonMagicSpec> amazonMagic;
    std::shared_ptr<const CorpseSearchSpec> corpseSearch;
    std::shared_ptr<const LeapSpec> leap;
    std::shared_ptr<const GrimWardSpec> grimWard;
    std::shared_ptr<const AssassinSkillSpec> assassin;
    int rank = 0;
    int stunFrames = 0;
    Fixed8 healingMinimum, healingMaximum;
    float castDuration = 0, castImpact = 0, castRate = 0;
    Fixed8 manaCost, minimumDamage, maximumDamage;
    float coldDuration = 0, missileVelocity = 0, missileLifetime = 0;
    float poisonDuration = 0;
    std::optional<WeaponSkillSpec> weapon;
    std::optional<SummonCastSpec> summon;
    std::optional<FrozenOrbCastSpec> frozenOrb;
    std::optional<BlizzardSpec> blizzard;
    std::optional<ArcSpec> arc;
    std::optional<MeteorSpec> meteor;
    std::optional<HeavenSpec> heaven;
    std::optional<CurseSpec> curse;
    std::optional<FreezingAreaCastSpec> freezingArea;
    std::optional<FirewallSpec> firewall;
    int delayFrames = 0;
    int shieldPercent = 0, shieldManaFactor = 0;
    bool requiresShield = false;
    int stormPeriod = 0, stormRadius = 0;
    int telekinesisRange = 0, telekinesisKnockbackChance = 0;
    int concentrationState = -1, concentrationFactor = 0;
    int hydraFrames = 0, hydraLimit = 0;
    std::optional<MissileImpactSpec> missileImpact;
    int missileId = -1;
    float missileNextDelay = 0;
    int missileCount = 1;
    Fixed8 startMana;
    float missileAcceleration = 0, missileMaxVelocity = 0;
    int staticPercent = 0;
    float staticRadius = 0;
    Fixed8 staticMinDamage;
    int castOverlayId = -1, hitOverlayId = -1;
    float visualDuration = 0, hitOverlayDuration = 0;
    int hitClass = 0;
    int sourceId = -1;
    int castMissileId = -1;
    float castMissileDuration = 0;
    std::optional<CombatEffectSpec> appliedEffect;
    bool warcry = false;
    std::optional<SkillCharge> charge;
    std::vector<StatValue> nativeEffectWrites;
    std::optional<int32_t> coldDurationFrames,poisonDurationFrames;
};
} // namespace d2x
