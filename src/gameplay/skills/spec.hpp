#pragma once
#include "gameplay/skills/behavior.hpp"
#include "gameplay/skills/curse_spec.hpp"
#include "gameplay/skills/elemental_spec.hpp"
#include "gameplay/skills/weapon_spec.hpp"
#include "gameplay/skills/summon_spec.hpp"
#include "gameplay/skills/visual.hpp"
#include "gameplay/combat/missile_effects.hpp"
#include <optional>
#include <utility>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
// Typed values imported from Skills.txt and Missiles.txt. No archive data enters gameplay.
struct SkillSpec {
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
    std::optional<FirewallSpec> firewall;
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
    SkillOverlayVisual castOverlay, hitOverlay;
    SkillOverlayVisual stateOverlay;
    int sourceId = -1;
    CombatStateDefinition state;
    std::array<int, 8> armorParameters{};
    std::vector<int> armorSynergySkills;
    std::string activationSoundArt;
    std::vector<SkillImpactVisual> impacts;
    std::vector<ProjectileResource> submissileResources;
    std::string impactSoundArt, releaseSoundArt;
};
} // namespace d2x
