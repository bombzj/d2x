#pragma once
#include "gameplay/skills/summon_spec.hpp"
#include "gameplay/effects/definition.hpp"
#include <array>
#include "gameplay/skills/aura.hpp"
#include "gameplay/skills/damage_curve.hpp"
#include "gameplay/combat/missile_effects.hpp"
namespace d2x {
struct ItemInstance;
enum class NecroSummonKind { Clay, Mage, Blood, Iron, Fire, Revive };
struct NecroMissileDefinition {
    int id = -1, hitShift = 8, frames = 0, maximum = 0;
    DamageType element = DamageType::Fire;
    SkillDamageCurve minimumDamage, maximumDamage;
    std::array<int, 3> framesPerLevel{};
    float velocity = 0, lifetime = 0;
    MissileImpactSpec impact;
};
struct NecroMissilePayload {
    int id = -1;
    float velocity = 0, lifetime = 0, minimumDamage = 0, maximumDamage = 0;
    DamageType element = DamageType::Fire;
    float duration = 0;
    MissileImpactSpec impact;
};
struct NecroSummonSpec {
    NecroSummonKind kind = NecroSummonKind::Clay;
    std::array<int, 8> parameters{};
    std::array<int, 6> masteryParameters{};
    std::array<int, 4> synergySkills{}, synergyPercent{}; // Clay, Blood, Iron, Fire hard ranks.
    CombatStateDefinition slowState, reviveState;
    std::array<NecroMissileDefinition, 4> missiles;
    SkillDamageCurve fireMinimum, fireMaximum;
    std::vector<AuraDefinition> fireAuras;
    int explosionId = -1; float explosionDuration = 0;
    int healOverlay = -1; float healOverlayDuration = 0;
    std::array<std::pair<int,float>,3> reviveVisuals{};
};
struct NecroPetSpec {
    NecroSummonKind kind = NecroSummonKind::Clay;
    int slowPercent = 0, velocityPercent = 0;
    int lifeLeechPercent = 0, ownerSharePercent = 0;
    int lifePercent = 0, damagePercent = 0, lifetimeFrames = 0, resistPercent = 0;
    CombatStateDefinition slowState, reviveState;
    std::array<NecroMissilePayload, 4> missiles;
    std::optional<AuraDefinition> aura;
    int explosionId = -1; float explosionDuration = 0;
    int healOverlay = -1; float healOverlayDuration = 0;
    std::array<std::pair<int,float>,3> reviveVisuals{};
};
struct NecroPetState {
    std::shared_ptr<const NecroPetSpec> spec;
    std::shared_ptr<const ItemInstance> item;
    EffectFrame nextAura = 0; std::optional<EffectFrame> explosionAt, expiresAt;
};
}
