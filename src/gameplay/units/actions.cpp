#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/skills/weapon_caster.hpp"
#include <algorithm>

namespace d2x {
bool actionReady(float castTime, float attackTime, float hitTime) {
    return castTime <= 0 && attackTime <= 0 && hitTime <= 0;
}
const WeaponDamage *selectAttackWeapon(const EquipmentStats &equipment, bool thrown, bool leftHand) {
    for (int index = 0; index < equipment.weaponCount; ++index) {
        const auto &weapon = equipment.weapons[index];
        if (leftHand && (!weapon.item || !weapon.leftHand)) continue;
        if (thrown && !weapon.throwable) return nullptr;
        if (!thrown && weapon.potion) return nullptr; // SrvDo001 rejects missile potions.
        return &weapon;
    }
    return nullptr;
}
bool advanceTimedAction(TimedActionView action, float dt, float completionEpsilon, float releaseEpsilon) {
    action.remaining = std::max(0.f, action.remaining - dt);
    if (action.remaining < completionEpsilon) action.remaining = 0;
    if (!(action.impact >= 0)) return false;
    action.impact -= dt;
    if (!(action.impact <= releaseEpsilon)) return false;
    action.impact = -1;
    return true;
}
void cancelTimedAction(TimedActionView action) {
    action.remaining = action.duration = 0;
    action.impact = -1;
}
void rescaleTimedAction(TimedActionView action, float scale) {
    action.remaining *= scale;
    action.duration *= scale;
    if (action.impact >= 0) action.impact *= scale;
}
bool advanceWeaponAction(WeaponAttackState &attack) {
    ++attack.ticks;
    if (attack.sequence) {
        const int previous = (attack.ticks - 1) * attack.timing.speed / 256;
        const int current = std::min(int(attack.sequence->frames.size()) - 1, attack.ticks * attack.timing.speed / 256);
        for (int frame = previous + 1; frame <= current; ++frame)
            if (attack.sequence->frames[size_t(frame)].hit) { attack.released = true; return true; }
        return false;
    }
    if (attack.released || attack.ticks < attack.timing.actionTick()) return false;
    attack.released = true;
    return true;
}
float weaponActionRemaining(const WeaponAttackState &attack) {
    return float(std::max(0, attack.timing.durationTicks() - attack.ticks)) / 25.f;
}
void cancelWeaponAction(WeaponSkillCaster actor) {
    actor.weaponAttack.reset();
    actor.meleeTime = 0;
}
void clearAttackIntent(WeaponSkillCaster actor) {
    actor.route.clear();
    actor.attackTarget = {};
    actor.attackPosition.reset();
    actor.approachSkill.reset();
    actor.throwAttack = actor.leftHandAttack = false;
}
void stopActorMovement(WeaponSkillCaster actor) {
    clearAttackIntent(actor);
    actor.moving = false;
    actor.charge.reset();
}
} // namespace d2x
