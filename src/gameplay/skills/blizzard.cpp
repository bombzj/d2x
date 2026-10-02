#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
Vec subcell(Vec point) { return {std::floor(point.x) + .5f, std::floor(point.y) + .5f}; }
} // namespace
bool Simulation::blizzardTargetClear(Vec origin, Vec target) const {
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !active(target)) return false;
    const int dx = std::abs(int(std::floor(target.x)) - int(std::floor(origin.x)));
    const int dy = std::abs(int(std::floor(target.y)) - int(std::floor(origin.y)));
    // Skills.cpp sub_6FD15340 and CreateMissile: size-one mask 5 at
    // the target, followed by UNITS_GetDistanceToCoordinates's integer metric.
    return std::max(dx, dy) + std::min(dx, dy) / 2 <= 100 &&
           grid_->missileSegment(subcell(target), subcell(target), {5, 1});
}
void Simulation::advanceArc(Missile &missile, std::vector<Missile> &spawned) {
    auto &arc = *missile.arc;
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool wall = clipMissilePath(missile.missileId, missile.pos, next);
    missile.age += 1.f / 25.f;
    missile.remaining = std::max(0.f, missile.remaining - 1.f / 25.f);
    if (missile.remaining <= .00001f) { missile.remaining = 0; return; }
    const auto collision = missileCollisions_.find(missile.missileId);
    if (collision == missileCollisions_.end()) { missile.remaining = 0; return; }
    std::vector<std::pair<float, EntityId>> contacts;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || !active(*target.position) ||
            target.id == missile.lastHit) continue;
        if (arc.spec.nextDelay > 0 && state_.area.novaHitUntil[target.id] > state_.time) continue;
        if (const auto contact = missileUnitIntersection(missile.pos, next, collision->second.size,
                *target.position, target.stats.collisionSize)) contacts.emplace_back(*contact, target.id);
    }
    std::sort(contacts.begin(), contacts.end());
    for (const auto &[fraction, id] : contacts) {
        reactToMissile(missile, id, spawned);
        missile.lastHit = id;
        if (arc.spec.nextDelay > 0) state_.area.novaHitUntil[id] = state_.time + float(arc.spec.nextDelay) / 25.f;
        const float damage = float(arc.minimumDamage + limitedRandom(missile.combatRandom,
            unsigned(std::max(0, arc.maximumDamage - arc.minimumDamage)))) / 256.f;
        if (missile.hitOverlayId >= 0)
            state_.area.effects.push_back({unitPosition(id), 0, missile.hitOverlayDuration,
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
                    !grid_->missileSegment(contact, *target.position, {0x04, 1})) continue;
                if (!fallback || target.id < fallback) fallback = target.id;
                if (target.id > id && (!successor || target.id < successor)) successor = target.id;
            }
            if (!successor) successor = fallback;
        }
        if (successor && resolveMissileSkill_) {
            const auto skill = resolveMissileSkill_(missile.owner, missile.skillId, missile.skillRank);
            const Vec origin{std::floor(contact.x) + .5f, std::floor(contact.y) + .5f};
            Missile child{ids_.allocate(), missile.owner, origin,
                (unitPosition(successor) - origin).unit() * skill.missileVelocity,
                skill.missileLifetime, missile.behavior, false, missile.missileId};
            child.combatRandom = childRandom(unitRandom_);
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
void Simulation::advanceMeteor(Missile &missile, std::vector<Missile> &spawned) {
    missile.age += 1.f / 25.f;
    missile.remaining = std::max(0.f, missile.remaining - 1.f / 25.f);
    if (missile.remaining > .00001f) return;
    missile.remaining = 0;
    if (!combatUnit(missile.owner)) return;
    const auto &program = *missile.meteor;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || !active(*target.position)) continue;
        const int deltaX = int(target.position->x) - int(missile.pos.x);
        const int deltaY = int(target.position->y) - int(missile.pos.y);
        if (deltaX * deltaX + deltaY * deltaY <= program.radius * program.radius)
            dealDamage({missile.owner, target.id, missile.damage, MonsterDamageType::Fire});
    }
    emit(MissileImpact{missile.missileId, missile.pos});
    constexpr Vec offsets[]{{2,-2},{-2,-2},{0,2},{0,5},{-3,3},{0,3},{3,3},{-1,2},{1,1},
        {-1,-1},{2,-1},{-4,-2},{-3,-2},{-1,-3},{0,-4},{1,-3},{3,-3},{4,-2}};
    auto fire = program.fire;
    if (resolveMissileSkill_) fire = resolveMissileSkill_(missile.owner, missile.skillId, missile.skillRank).meteor->fire;
    for (int index = 0; index < 18; index += program.fireStep) {
        const Vec position = missile.pos + offsets[index];
        if (!grid_->missileSegment(position, position, {5, 1})) continue;
        Missile child{ids_.allocate(), missile.owner, position, {}, float(fire.fireFrames) / 25.f,
            SkillBehavior::Meteor, false, fire.fireId};
        child.combatRandom = childRandom(unitRandom_);
        child.firewall = Missile::FirewallState{fire, false, 0};
        spawned.push_back(std::move(child));
    }
}
void Simulation::launchBlizzard(PlayerState &player, const SkillCastSpec &skill, Vec target) {
    if (!skill.blizzard) throw std::runtime_error("Missing Blizzard missile program");
    Missile center{ids_.allocate(), player.id, subcell(target), {}, skill.missileLifetime,
                   SkillBehavior::Blizzard, false, skill.missileId};
    center.combatRandom = childRandom(unitRandom_);
    center.skillId = skill.sourceId; center.skillRank = skill.rank;
    center.killOnHit = false;
    center.blizzard.emplace();
    center.blizzard->spec = *skill.blizzard;
    center.blizzard->lifetimeFrames = int(skill.missileLifetime * 25.f + .5f);
    center.blizzard->spawnSeedX = int(std::floor(target.x + missileWorldOrigin_.x));
    state_.area.missiles.push_back(std::move(center));
}
void Simulation::advanceBlizzard(Missile &missile, std::vector<Missile> &spawned) {
    auto &state = *missile.blizzard;
    if (state.elapsedFrames >= state.lifetimeFrames) { missile.remaining = 0; return; }
    const auto &program = state.spec;
    if (state.center && (state.lifetimeFrames - state.elapsedFrames) % program.emissionPeriod == 0 &&
        combatUnit(missile.owner)) {
        // SrvDo10 -> CreateMissileWithCollisionCheck: reseed on each attempt;
        // radius 7 samples [-6,5] on both integer axes, not a uniform disc.
        missile.combatRandom = initialRandom(uint32_t(state.spawnSeedX +
            state.lifetimeFrames - state.elapsedFrames));
        const int range = program.radius - 1;
        const Vec point = subcell(missile.pos) + Vec{
            float(int(limitedRandom(missile.combatRandom, uint32_t(2 * range))) - range),
            float(int(limitedRandom(missile.combatRandom, uint32_t(2 * range))) - range)};
        // Native maker tests a point with mask 5, not the shard's size-two footprint.
        if (grid_->missileSegment(point, point, {5, 1})) {
            if (!resolveMissileSkill_) throw std::runtime_error("Blizzard owner has no skill resolver");
            const auto skill = resolveMissileSkill_(missile.owner, missile.skillId, missile.skillRank);
            Missile shard{ids_.allocate(), missile.owner, point, {}, float(program.shardFrames) / 25.f,
                          SkillBehavior::Blizzard, false, program.shardId};
            shard.combatRandom = childRandom(unitRandom_);
            shard.skillId = missile.skillId; shard.skillRank = missile.skillRank;
            shard.killOnHit = false;
            shard.fixedElement = MonsterDamageType::Cold;
            shard.blizzard.emplace();
            auto &child = *shard.blizzard;
            child.spec = program; child.center = false; child.lifetimeFrames = program.shardFrames;
            child.minimumDamage = int(skill.minimumDamage * 256.f);
            child.maximumDamage = int(skill.maximumDamage * 256.f);
            child.coldFrames = int(skill.coldDuration * 25.f + .5f);
            emit(BlizzardShardCreated{shard.missileId, point});
            spawned.push_back(std::move(shard));
        }
    }
    ++state.elapsedFrames;
    missile.age = float(state.elapsedFrames) / 25.f;
    missile.remaining = float(state.lifetimeFrames - state.elapsedFrames) / 25.f;
    // HandleMissileCollision decrements before unit search; no damage on expiry.
    if (state.center || state.elapsedFrames == state.lifetimeFrames) return;
    if (const auto contact = missileTarget(missile, missile.pos)) {
        reactToMissile(missile, contact->first, spawned);
        const auto target = combatUnit(contact->first);
        const int minimum = std::min(state.minimumDamage, state.maximumDamage);
        const uint32_t span = uint32_t(std::abs(state.maximumDamage - state.minimumDamage));
        const float damage = float(minimum + limitedRandom(missile.combatRandom, span)) / 256.f;
        missile.lastHit = contact->first; // LastCollide, not an all-target hit set or NextDelay.
        missile.damage = damage;
        dealDamage({missile.owner, contact->first, damage, MonsterDamageType::Cold,
                    missileColdDuration(missile.owner, target, state.coldFrames)});
    } else if (!missilePathClear(missile.missileId, missile.pos, missile.pos)) missile.remaining = 0;
}
} // namespace d2x
