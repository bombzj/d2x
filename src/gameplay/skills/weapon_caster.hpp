#pragma once
#include "gameplay/skills/caster.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/combat/weapon_attack.hpp"
#include <deque>

namespace d2x {
struct WeaponSkillCaster {
    SkillCaster casting;
    EquipmentStats &equipment;
    std::optional<WeaponAttackState> &weaponAttack;
    std::optional<ChargeSkillState> &charge;
    std::optional<SkillCastSpec> &approachSkill;
    std::deque<Vec> &route;
    EntityId &attackTarget;
    std::optional<Vec> &attackPosition;
    bool &attackStationary, &throwAttack, &leftHandAttack, &moving, &runningNow;
    float &meleeTime, &life;
    int level;
};
} // namespace d2x
