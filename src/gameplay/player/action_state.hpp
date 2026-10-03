#pragma once
#include "gameplay/combat/weapon_attack.hpp"
#include "gameplay/skills/cast_state.hpp"
#include <optional>

namespace d2x {
struct PlayerActions {
    float castTime = 0, hitTime = 0, deathTime = 0, meleeTime = 0;
    std::optional<WeaponAttackState> weaponAttack;
    std::optional<SkillCastSpec> approachSkill;
    std::optional<WeaponAttackState> blockAnimation;
    unsigned vengeanceHit = 0;
    std::optional<ChargeSkillState> charge;
    EntityId attackTarget;
    std::optional<Vec> attackPosition;
    bool attackStationary = false, throwAttack = false, leftHandAttack = false;
    bool dead = false;
};
} // namespace d2x
