#include "gameplay/player/control.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/character/attributes.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/units/movement.hpp"
#include <algorithm>

namespace d2x {
bool advancePlayerControl(PlayerControlContext context, PlayerControlInput input, float dt, IPlayerControlWorld &world) {
    auto actor = context.actions;
    auto &casting = actor.casting;
    if (casting.blockAnimation) return false;
    if (actor.charge) { world.advanceCharge(dt); return false; }
    Vec step;
    float remaining = 0;
    bool followingRoute = false;
    if (actor.attackTarget || actor.attackPosition) {
        auto target = world.unit(actor.attackTarget);
        const auto *weapon = world.weapon(actor.throwAttack, actor.leftHandAttack);
        if (!weapon || (actor.attackTarget && (!target.alive() || !world.canAttack(casting.id, target.id)))) {
            actor.attackTarget = {};
            actor.attackPosition.reset();
            actor.approachSkill.reset();
            actor.route.clear();
        } else {
            const Vec aim = target ? *target.position : *actor.attackPosition;
            const bool projectile = (actor.throwAttack || weapon->ranged) && !(actor.approachSkill && actor.approachSkill->weapon->smite);
            const bool inRange = actor.attackStationary || !target || (projectile ?
                (weapon->projectile && missileDistance(casting.pos, aim) <
                    weapon->projectile->speed * weapon->projectile->lifetime) : world.meleeReach(target.id, *weapon));
            if (inRange) {
                actor.route.clear();
                if (actionReady(casting.castTime, actor.meleeTime, casting.hitTime)) {
                    if (actor.approachSkill) {
                        const auto skill = *actor.approachSkill;
                        actor.approachSkill.reset();
                        world.beginWeaponSkill(skill, aim, actor.attackTarget);
                    } else world.beginAttack(aim, actor.attackTarget, *weapon, actor.throwAttack, actor.leftHandAttack);
                    actor.attackTarget = {};
                    actor.attackPosition.reset();
                }
            } else if (actor.route.empty() || (actor.route.back() - aim).length() > 1)
                actor.route = world.path(casting.pos, aim);
        }
    }
    if (input.direction.length() > .1f && actionReady(casting.castTime, actor.meleeTime, casting.hitTime)) {
        clearAttackIntent(actor);
        step = input.direction.unit();
    } else if (!actor.route.empty() && actionReady(casting.castTime, actor.meleeTime, casting.hitTime)) {
        discardReachedWaypoints(casting.pos, actor.route, .01f);
        if (!actor.route.empty()) {
            const auto delta = actor.route.front() - casting.pos;
            remaining = delta.length(); followingRoute = true; step = delta.unit();
        }
    }
    const bool running = (context.running || input.forceRun) && (input.safeZone || context.stamina > 0);
    actor.runningNow = running;
    float speed = running ? context.attributes.runSpeed : context.attributes.walkSpeed;
    if (context.chill > 0) speed *= .5f;
    if (context.webRemaining > 0)
        speed *= std::max(0.f, 1.f + float(context.webPercent) / 100.f);
    if (step.length() > .1f) {
        const float distance = followingRoute ? std::min(dt * speed, remaining) : dt * speed;
        const auto moved = advanceMovement(casting.pos, step, distance,
            [&](Vec from, Vec to) { return world.segment(from, to); }, followingRoute ? 0 : 12, .0001f);
        if (moved.accepted) {
            if (followingRoute && moved.distance >= remaining) actor.route.pop_front();
            actor.moving = true; casting.look = step;
        } else if (moved.distance > .0001f) actor.route.clear();
    }
    return true;
}
} // namespace d2x
