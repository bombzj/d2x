#pragma once
#include "gameplay/model/definitions.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/combat/missile_effects.hpp"
#include "gameplay/combat/unit.hpp"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
struct CurseSpec {
    CombatStateDefinition state;
    int radius = 0, radiusPerLevel = 0, frames = 0, framesPerLevel = 0;
    CharacterModifiers modifiers;
    CurseAi ai = CurseAi::None;
    int resistMinimum = 0, resistMaximum = 0;
    int reflectPercent = 0, reflectPerLevel = 0;
    int lifeTapPercent = 0, lifeTapPerLevel = 0;
    int healOverlay = -1;
    float healOverlayDuration = 0;
};
struct FreezingAreaSpec {
    int radius = 0, radiusPerLevel = 0, radiusOverride = 0;
    int freezeFrames = 0, freezeFramesPerLevel = 0, freezeOverride = 0;
    int synergySkill = -1, synergyPercent = 0;
    int ejectaId = -1;
};
struct FreezingAreaCastSpec {
    int radius = 0, freezeFrames = 0;
};
struct BlizzardSpec {
    int radius = 0, emissionPeriod = 0;
    int shardId = -1, shardFrames = 0;
    int fallDistance = 0, fallRate = 0;
    int impactId = -1, impactFrames = 0;
};
struct ArcSpec {
    int visualId = -1, subloops = 1;
    int range = 0, count = 1, countPerLevel = 0;
    int nextDelay = 0;
};
struct MeteorSpec {
    int radius = 0, radiusPerLevel = 0, fireStep = 1;
    int fireFrames = 0, fireFramesPerLevel = 0;
    MonsterFirewall fire;
    std::array<int, 5> fireMinimumPerLevel{}, fireMaximumPerLevel{};
    int fireSynergySkill = -1, fireSynergyPercent = 0;
    int fallId = -1, tailId = -1, explodeId = -1;
    int fallStart = 0, fallSpeed = 0, explodeDensity = 1;
    int lightId = -1, mediumId = -1, smallId = -1;
    int mediumDensity = 1, smallDensity = 1;
};
struct HeavenSpec {
    int delayFrames = 0, radius = 0, limit = 0, limitPerLevel = 0;
    int boltId = -1, boltFrames = 0, boltVelocity = 0;
    int minimum = 0, maximum = 0, synergySkill = -1, synergyPercent = 0;
    std::array<int, 5> minimumPerLevel{}, maximumPerLevel{};
    int healingMinimum = 0, healingMinimumPerLevel = 0;
    int healingMaximum = 0, healingMaximumPerLevel = 0;
};
// Weapon skills use the ordinary attack animation, equipment and ammunition pipeline.
struct WeaponSkillSpec {
    std::string requiredType;
    bool thrown = false, manaOnRelease = false;
    int attackRating = 0, attackRatingPerLevel = 0, delayFrames = 0;
    int damagePercent = 0, damagePerLevel = 0, selfDamagePercent = 0;
    std::map<int, int> damageSynergies;
    int damageStartLevel = 1, attacks = 1, attackLimit = 1, rollbackPercent = 0;
    bool interruptible = true;
    std::array<int, 3> elementPercent{};
    int elementPerLevel = 0;
    std::array<std::map<int, int>, 3> elementSynergies;
    bool smite = false;
    std::string mode;
    int stunFrames = 0, stunPerLevel = 0;
    int conversionMinimum = 0, conversionMaximum = 0, conversionChance = 0, conversionFrames = 0;
    CombatStateDefinition conversionState;
    int chargeVelocity = 0;
};
struct SummonSkillSpec {
    std::string monster, iconArt;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    int masterySkill = -1, resistSkill = -1;
    int masteryLife = 0, masteryDamage = 0;
    int lifePerRank = 0, damagePerRank = 0, attackPerRank = 0, defensePerRank = 0;
    int shieldChance = 0, shieldVariants = 0, resistMinimum = 0, resistMaximum = 0;
    std::array<int, 5> damageSteps{};
    std::array<UnitCombatStats, 3> base;
    std::vector<std::array<int, 3>> levelDefense, levelAttack;
};
struct SummonCastSpec {
    std::string monster;
    MonsterKind kind = MonsterKind::NecroSkeleton;
    UnitCombatStats stats;
    int limit = 0, shieldChance = 0, shieldVariants = 0;
};
SummonCastSpec resolveSummon(const SummonSkillSpec &spec, int rank, int mastery, int resist,
                            int ownerLevel, int difficulty);
struct FrozenOrbSpec {
    struct Child {
        int missileId = -1, velocity = 0, velocityPerLevel = 0;
        int lifetimeFrames = 0, rangePerLevel = 0;
    };
    Child bolt, nova;
    int emissionPeriod = 1, directionStep = 0, burstStep = 1;
    int novaTurnFrames = 0, novaTurnPeriod = 1;
};
struct FrozenOrbCastSpec {
    struct Child {
        int missileId = -1, lifetimeFrames = 0;
        float speed = 0;
    };
    Child bolt, nova;
    int lifetimeFrames = 0, emissionPeriod = 1, directionStep = 0, burstStep = 1;
    int novaTurnFrames = 0, novaTurnPeriod = 1;
};
// Typed values imported from Skills.txt and Missiles.txt. No archive data enters gameplay.
struct SkillSpec {
    struct OverlayVisual {
        int id = -1, frames = 0, trans = 5;
        float fps = 0;
        Vec offset;
        std::array<int, 4> heights{};
        bool preDraw = false;
        std::string art;
    };
    struct ImpactVisual {
        int missileId = -1;
        std::string art;
        float duration = 0;
    };
    SkillBehavior effect = SkillBehavior::None;
    int mana = 0, minimumMana = 0, manaPerLevel = 0, manaShift = 8;
    int minimumDamage = 0, maximumDamage = 0, hitShift = 8;
    bool fireDamage = false;
    bool lightningDamage = false;
    bool coldDamage = false;
    bool poisonDamage = false;
    int poisonFrames = 0;
    std::array<int, 3> poisonFramesPerLevel{};
    std::optional<WeaponSkillSpec> weapon;
    std::optional<SummonSkillSpec> summon;
    std::optional<FrozenOrbSpec> frozenOrb;
    std::optional<BlizzardSpec> blizzard;
    std::optional<ArcSpec> arc;
    std::optional<MeteorSpec> meteor;
    std::optional<HeavenSpec> heaven;
    std::optional<CurseSpec> curse;
    std::optional<FreezingAreaSpec> freezingArea;
    std::optional<MonsterFirewall> firewall;
    int firewallRangePerLevel = 0;
    std::optional<std::pair<int, int>> diminishingDuration;
    std::optional<std::pair<int, int>> linearDuration;
    int shieldMaximum = 0, shieldManaFactor = 0, shieldSynergySkill = -1;
    bool requiresShield = false, holyShield = false;
    int enchantAttackRating = 0, enchantAttackRatingPerLevel = 0;
    std::array<int, 4> healingParameters{};
    int healingSynergySkill = -1, healingSynergyPercent = 0;
    std::array<int, 5> stormParameters{};
    int telekinesisRange = 0, telekinesisKnockbackChance = 0;
    int concentrationState = -1, concentrationFactor = 0;
    std::optional<std::pair<int, int>> hydraDuration;
    int hydraLimit = 0;
    std::map<int, int> weightedSynergies;
    int delayFrames = 0;
    std::array<int, 5> minimumPerLevel{}, maximumPerLevel{};
    int synergyPercent = 0;
    std::vector<int> synergySkills;
    int coldFrames = 0;
    std::array<int, 3> coldFramesPerLevel{};
    int coldSynergySkill = -1, coldSynergyPercent = 0;
    int staticPercent = 0, staticRange = 0, staticRangePerLevel = 0, staticMinDamage = 0;
    int missileId = -1;
    int missileNextDelay = 0;
    int missileCount = 1, missileCountPerLevel = 0, missileCountLimit = 1;
    int startMana = 0, flameFrames = 0, flameFramesPerLevel = 0;
    int missileVelocityPerLevel = 0, missileRangePerLevel = 0, missileAcceleration = 0, missileMaxVelocity = 0;
    float missileVelocity = 0, missileLifetime = 0;
    std::optional<MissileImpactSpec> missileImpact;
    std::string missileArt, castSoundArt;
    OverlayVisual castOverlay, hitOverlay;
    OverlayVisual stateOverlay;
    int sourceId = -1;
    CombatStateDefinition state;
    std::array<int, 8> armorParameters{};
    std::vector<int> armorSynergySkills;
    std::string activationSoundArt;
    std::vector<ImpactVisual> impacts;
    std::vector<ProjectileResource> submissileResources;
    std::string impactSoundArt, releaseSoundArt;
};
struct SkillCastSpec {
    SkillBehavior effect = SkillBehavior::None;
    int rank = 0;
    float healingMinimum = 0, healingMaximum = 0;
    float castDuration = 0, castImpact = 0, castRate = 0;
    float manaCost = 0, minimumDamage = 0, maximumDamage = 0;
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
    std::optional<MonsterFirewall> firewall;
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
    float startMana = 0;
    float missileAcceleration = 0, missileMaxVelocity = 0;
    float staticPercent = 0, staticRadius = 0, staticMinDamage = 0;
    int castOverlayId = -1, hitOverlayId = -1;
    float visualDuration = 0, hitOverlayDuration = 0;
    int sourceId = -1;
    std::optional<CombatEffectSpec> appliedEffect;
};
SkillCastSpec resolveSkill(const SkillSpec &spec, int rank,
                           const std::map<int, int> &learned, int fireMasteryPercent = 0,
                           int lightningMasteryPercent = 0, int coldDamagePercent = 0);
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames);
} // namespace d2x
