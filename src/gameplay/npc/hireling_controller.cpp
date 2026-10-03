#include "gameplay/npc/hireling_control.hpp"
#include "gameplay/units/movement.hpp"
#include "gameplay/combat/geometry.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void advanceHirelingControl(HirelingControlView merc, HirelingOwnerView owner,
                           HirelingMovementRules rules, float dt, const HirelingControlWorld &world) {
    auto around = [&](Vec center, int radius, Vec away) -> std::optional<Vec> {
        constexpr Vec offsets[]{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
        std::optional<Vec> best;
        float score = -1e30f;
        for (auto offset : offsets) {
            const Vec position{std::floor(center.x + offset.x * radius) + .5f,
                               std::floor(center.y + offset.y * radius) + .5f};
            if (!world.open(position)) continue;
            const float candidate = (position - away).length();
            if (candidate > score) { score = candidate; best = position; }
        }
        return best;
    };
    const int ownerDistance = std::max(0, missileDistance(merc.pos, owner.pos) - 2);
    // AiThink Fn061: catch up before acquiring enemies; teleport only beyond 100.
    if (ownerDistance > 100) {
        if (auto position = around(owner.pos, 4, merc.pos)) {
            merc.pos = *position; merc.route.clear(); merc.moving = false; merc.thinkTimer = 5.f / 25.f;
        }
        return;
    }
    const bool hurry = ownerDistance > 24 || (ownerDistance > 16 && owner.moving);
    merc.thinkTimer = std::max(0.f, merc.thinkTimer - dt);
    if (hurry && (merc.route.empty() || (merc.route.back() - owner.pos).length() > 16)) {
        if (auto position = around(owner.pos, 4, merc.pos)) merc.route = world.path(merc.pos, *position);
    }
    if (!merc.route.empty()) {
        discardReachedWaypoints(merc.pos, merc.route, .01f);
        if (!merc.route.empty()) {
            const auto offset = merc.route.front() - merc.pos;
            const int frw = std::max(0, rules.fasterMoveVelocity);
            const bool boosted = hurry && (!owner.moving || owner.runningNow);
            int rate = 75 + (boosted ? 60 : 0) + 150 * frw / (150 + frw) + rules.velocityPercent;
            if (merc.webSlowRemaining > 0) rate += merc.webSlowPercent;
            rate = world.movementRate(rate);
            const float speed = float((rules.walkVelocity << 8) * rate / 100) * 25.f / 4096.f;
            if (advanceMovement(merc.pos, offset.unit(), std::min(offset.length(), speed * dt),
                [&](Vec from, Vec to) { return world.segment(from, to) && world.open(to); }).accepted) {
                merc.look = offset.unit();
                if (!merc.moving) merc.animationTime = 0;
                merc.moving = true;
                merc.animationRate = rules.walkAnimationRate * float(rate) / 100.f * 25.f / 256.f;
                merc.animationTime += dt * merc.animationRate;
                return;
            }
            merc.route.clear(); merc.thinkTimer = 5.f / 25.f;
        }
    }
    if (merc.moving) merc.animationTime = 0;
    merc.moving = false;
    if (const auto idle = world.idleAnimationRate()) merc.animationRate = *idle;
    merc.animationTime += dt * merc.animationRate;
    if (merc.thinkTimer > 0) return;
    merc.thinkTimer = 5.f / 25.f;
    if (hurry) return;
    if (const auto target = world.target()) {
        const int chance = std::min(merc.attackBias + 40 + 2 * merc.level, 95);
        const bool attackNow = limitedRandom(merc.combatRandom, 100) < unsigned(chance);
        merc.attackBias = attackNow ? 0 : merc.attackBias + 10;
        if (target->distance < 4 && limitedRandom(merc.combatRandom, 100) < 50) {
            if (ownerDistance > 4)
                if (auto position = around(owner.pos, 4, target->pos)) merc.route = world.path(merc.pos, *position);
            if (merc.route.empty())
                if (auto position = around(merc.pos, 4, target->pos)) merc.route = world.path(merc.pos, *position);
            if (!merc.route.empty()) return;
        }
        if (!attackNow) { merc.thinkTimer = 10.f / 25.f; return; }
        world.beginAttack(target->id, target->pos);
        merc.look = (target->pos - merc.pos).unit();
        return;
    }
    if (ownerDistance <= 1 || limitedRandom(merc.combatRandom, 100) < 5)
        if (auto position = around(owner.pos, 4, merc.pos)) merc.route = world.path(merc.pos, *position);
}
} // namespace d2x
