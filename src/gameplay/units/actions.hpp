#pragma once

namespace d2x {
struct WeaponAttackState;
struct WeaponSkillCaster;
struct EquipmentStats;
struct WeaponDamage;
struct TimedActionView {
    float &remaining, &duration, &impact;
};
// A controller chooses which clocks block its intent; storage roles are not
// interpreted here. Callers keep their native block/charge/death qualifiers.
bool actionReady(float castTime, float attackTime, float hitTime);
const WeaponDamage *selectAttackWeapon(const EquipmentStats &equipment, bool thrown, bool leftHand);
bool advanceTimedAction(TimedActionView action, float dt, float completionEpsilon, float releaseEpsilon);
void cancelTimedAction(TimedActionView action);
void rescaleTimedAction(TimedActionView action, float scale);
bool advanceWeaponAction(WeaponAttackState &attack);
float weaponActionRemaining(const WeaponAttackState &attack);
void cancelWeaponAction(WeaponSkillCaster actor);
void clearAttackIntent(WeaponSkillCaster actor);
void stopActorMovement(WeaponSkillCaster actor);
} // namespace d2x
