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
// Weapon skills use the ordinary attack animation, equipment and ammunition pipeline.
struct WeaponSkillSpec {
    std::string requiredType;
    bool thrown = false, manaOnRelease = false;
    int attackRating = 0, attackRatingPerLevel = 0, delayFrames = 0;
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
// Typed values imported from Skills.txt and Missiles.txt. No archive data enters gameplay.
struct SkillSpec {
    struct OverlayVisual {
        int id = -1, frames = 0, trans = 5;
        float fps = 0;
        Vec offset;
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
    bool poisonDamage = false;
    int poisonFrames = 0;
    std::array<int, 3> poisonFramesPerLevel{};
    std::optional<WeaponSkillSpec> weapon;
    std::optional<SummonSkillSpec> summon;
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
    std::string impactSoundArt, releaseSoundArt;
};
struct SkillCastSpec {
    SkillBehavior effect = SkillBehavior::None;
    int rank = 0;
    float castDuration = 0, castImpact = 0, castRate = 0;
    float manaCost = 0, minimumDamage = 0, maximumDamage = 0;
    float coldDuration = 0, missileVelocity = 0, missileLifetime = 0;
    float poisonDuration = 0;
    std::optional<WeaponSkillSpec> weapon;
    std::optional<SummonCastSpec> summon;
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
                           int lightningMasteryPercent = 0);
std::vector<Vec> chargedBoltPath(Vec origin, Vec target, int index, int frames);
} // namespace d2x
