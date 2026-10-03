#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/missile_rules.hpp"

namespace d2x {
bool SkillRuntime::advanceSpecialMissile(Missile &missile, float dt, std::vector<Missile> &spawned) {
    if (missile.heaven) advanceHeaven(missile, dt, spawned);
    else if (missile.arc) advanceArc(missile, spawned);
    else if (missile.meteor) advanceMeteor(missile, spawned);
    else if (missile.firewall) advanceFirewall(missile, spawned);
    else if (missile.frozenOrb) advanceFrozenOrb(missile, spawned);
    else if (missile.blizzard) advanceBlizzard(missile, spawned);
    else if (missile.freezingArea) advanceGlacialSpike(missile, spawned);
    else if (missile.coldRetaliation) advanceChillingArmorBolt(missile, spawned);
    else return false;
    return true;
}
void SkillRuntime::hitMissile(Missile &missile, EntityId target, std::vector<Missile> &spawned) {
    reactToMissile(missile, target, spawned);
    missile.lastHit = target;
    if (missile.nextHitDelay > 0) world_.nextHitTime(target) = world_.time() + missile.nextHitDelay;
    if (missile.monsterAttack && !missile.fixedElement) world_.nativeMissileHit(missile, target);
    else if (missile.impact) world_.impact(missile, spawned, target);
    else {
        DamageRequest hit{missile.owner, target, missile.damage, missileElement(missile), missile.chill};
        if (missile.behavior == SkillBehavior::IceBlast) {
            hit.chill = 0;
            hit.freezeFrames = int(missile.chill * 25.f + .5f);
        }
        dealDamage(hit);
    }
    if (missile.hitOverlayId >= 0)
        world_.addEffect({unitPosition(target), 0, missile.hitOverlayDuration, -1, missile.hitOverlayId, target});
}
} // namespace d2x
