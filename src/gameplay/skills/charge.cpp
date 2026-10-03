#include "gameplay/combat/unit.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include "gameplay/skills/weapon_port.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include <algorithm>

namespace d2x {
void SkillRuntime::advanceCharge(WeaponSkillCaster p, float dt) {
    ++p.charge->ticks;
    auto charge = *p.charge;
    const auto *weapon = attackWeapon(p, false, false);
    auto target = combatUnit(charge.enemy);
    if (p.life <= 0 || p.casting.hitTime > 0 || !weapon || (charge.enemy && (!target.alive() || !canAttack(p.casting.id, target.id)))) {
        p.charge.reset();
        return;
    }
    if (target) charge.target = *target.position;
    if (target && weapons_.meleeReach(p.casting.id, target.id, *weapon)) {
        p.charge.reset();
        beginWeaponSkill(p, charge.skill, charge.target, target.id);
        return;
    }
    const Vec delta = charge.target - p.casting.pos;
    const float distance = std::min(delta.length(), charge.speed * dt);
    const Vec next = p.casting.pos + delta.unit() * distance;
    if (distance < .01f || !weapons_.movementSegment(p.casting.id, p.casting.pos, next)) {
        p.charge.reset();
        return;
    }
    p.casting.pos = next; p.casting.look = delta.unit(); p.moving = true; p.runningNow = true;
    if (distance >= delta.length() && !target) p.charge.reset();
}
} // namespace d2x
