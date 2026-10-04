#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SkillRuntime::advanceArc(Missile &missile, std::vector<Missile> &spawned) {
    auto &arc = *missile.arc;
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool wall = world_.clipPath(missile.missileId, missile.pos, next);
    missile.age += 1.f / 25.f;
    missile.remaining = std::max(0.f, missile.remaining - 1.f / 25.f);
    if (missile.remaining <= .00001f) { missile.remaining = 0; return; }
    const auto collision = world_.missileSize(missile.missileId);
    if (!collision) { missile.remaining = 0; return; }
    std::vector<std::pair<float, EntityId>> contacts;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || !active(*target.position) ||
            target.id == missile.lastHit) continue;
        if (arc.spec.nextDelay > 0 && world_.nextHitTime(target.id) > world_.time()) continue;
        if (const auto contact = missileUnitIntersection(missile.pos, next, *collision,
                *target.position, target.stats.collisionSize)) contacts.emplace_back(*contact, target.id);
    }
    std::sort(contacts.begin(), contacts.end());
    for (const auto &[fraction, id] : contacts) {
        reactToMissile(missile, id, spawned);
        missile.lastHit = id;
        if (arc.spec.nextDelay > 0) world_.nextHitTime(id) = world_.time() + float(arc.spec.nextDelay) / 25.f;
        const float damage = float(arc.minimumDamage + limitedRandom(missile.combatRandom,
            unsigned(std::max(0, arc.maximumDamage - arc.minimumDamage)))) / 256.f;
        if (missile.hitOverlayId >= 0)
            world_.addEffect({unitPosition(id), 0, missile.hitOverlayDuration,
                -1, missile.hitOverlayId, id});
        const Vec contact = missile.pos + (next - missile.pos) * fraction;
        EntityId successor, fallback;
        if (missile.behavior == SkillBehavior::ChainLightning && arc.remainingHits > 1) {
            for (const auto &target : combatUnits()) {
                if (target.id == id || !target.alive() || !canAttack(missile.owner, target.id) ||
                    !active(*target.position)) continue;
                const int deltaX = int(target.position->x) - int(contact.x);
                const int deltaY = int(target.position->y) - int(contact.y);
                if (deltaX * deltaX + deltaY * deltaY > arc.spec.range * arc.spec.range ||
                    !world_.missileSegment(contact, *target.position, {0x04, 1})) continue;
                if (!fallback || target.id < fallback) fallback = target.id;
                if (target.id > id && (!successor || target.id < successor)) successor = target.id;
            }
            if (!successor) successor = fallback;
        }
        if (successor && world_.hasResolver()) {
            const auto skill = world_.resolve(missile.owner, missile.skillId, missile.skillRank);
            const Vec origin{std::floor(contact.x) + .5f, std::floor(contact.y) + .5f};
            Missile child{world_.allocate(), missile.owner, origin,
                (unitPosition(successor) - origin).unit() * skill.missileVelocity,
                skill.missileLifetime, missile.behavior, false, missile.missileId};
            child.combatRandom = world_.childSeed();
            child.skillId = missile.skillId; child.skillRank = missile.skillRank;
            child.lastHit = id;
            child.arc = Missile::ArcState{*skill.arc, arc.remainingHits - 1,
                int(skill.minimumDamage * 256.f), int(skill.maximumDamage * 256.f)};
            child.hitOverlayId = skill.hitOverlayId; child.hitOverlayDuration = skill.hitOverlayDuration;
            spawned.push_back(std::move(child));
            emit(MissileReleased{missile.missileId});
        }
        dealDamage({missile.owner, id, damage, MonsterDamageType::Lightning});
        if (missile.behavior == SkillBehavior::ChainLightning) { missile.remaining = 0; next = contact; break; }
    }
    missile.pos = next;
    if (wall) missile.remaining = 0;
}
} // namespace d2x
