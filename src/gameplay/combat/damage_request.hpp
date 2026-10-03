#pragma once
#include "core/id.hpp"
#include "gameplay/combat/damage_type.hpp"
#include <array>

namespace d2x {
enum class DamagePermission { Hostile, ExistingEffect, Environment, Debug };
struct DamageRequest {
    EntityId attacker, defender;
    float amount = 0;
    MonsterDamageType type = MonsterDamageType::Physical;
    float chill = 0;
    bool mitigated = false, hitRecovery = true, freeze = false;
    // Only an existing periodic effect, an environment hazard or an explicit
    // debug command may bypass allegiance. Ordinary attacks always check it.
    DamagePermission permission = DamagePermission::Hostile;
    std::array<float, 6> channels{}; // Optional simultaneous channels: one hit/death transition.
    int freezeFrames = 0; // Native unmitigated freeze length, resolved before the death transition.
    int hitClass = -1;
    bool softHit = false;
};
} // namespace d2x
