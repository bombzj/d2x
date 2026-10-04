#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
namespace {
Vec subcell(Vec point) { return {std::floor(point.x) + .5f, std::floor(point.y) + .5f}; }
} // namespace
bool SkillRuntime::blizzardTargetClear(Vec origin, Vec target) const {
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !active(target)) return false;
    const int dx = std::abs(int(std::floor(target.x)) - int(std::floor(origin.x)));
    const int dy = std::abs(int(std::floor(target.y)) - int(std::floor(origin.y)));
    // Skills.cpp sub_6FD15340 and CreateMissile: size-one mask 5 at
    // the target, followed by UNITS_GetDistanceToCoordinates's integer metric.
    return std::max(dx, dy) + std::min(dx, dy) / 2 <= 100 &&
           world_.missileSegment(subcell(target), subcell(target), {5, 1});
}
void SkillRuntime::launchBlizzard(SkillProjectileSource player, const SkillCastSpec &skill, Vec target) {
    if (!skill.blizzard) throw std::runtime_error("Missing Blizzard missile program");
    Missile center{world_.allocate(), player.id, subcell(target), {}, skill.missileLifetime,
                   SkillBehavior::Blizzard, false, skill.missileId};
    center.combatRandom = world_.childSeed();
    center.skillId = skill.sourceId; center.skillRank = skill.rank;
    center.killOnHit = false;
    center.blizzard.emplace();
    center.blizzard->spec = *skill.blizzard;
    center.blizzard->lifetimeFrames = int(skill.missileLifetime * 25.f + .5f);
    center.blizzard->spawnSeedX = int(std::floor(target.x + world_.missileOrigin().x));
    world_.addMissile(std::move(center));
}
void SkillRuntime::advanceBlizzard(Missile &missile, std::vector<Missile> &spawned) {
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
        if (world_.missileSegment(point, point, {5, 1})) {
            if (!world_.hasResolver()) throw std::runtime_error("Blizzard owner has no skill resolver");
            const auto skill = world_.resolve(missile.owner, missile.skillId, missile.skillRank);
            Missile shard{world_.allocate(), missile.owner, point, {}, float(program.shardFrames) / 25.f,
                          SkillBehavior::Blizzard, false, program.shardId};
            shard.combatRandom = world_.childSeed();
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
    if (const auto contact = world_.missileTarget(missile, missile.pos)) {
        reactToMissile(missile, contact->first, spawned);
        const auto target = combatUnit(contact->first);
        const int minimum = std::min(state.minimumDamage, state.maximumDamage);
        const uint32_t span = uint32_t(std::abs(state.maximumDamage - state.minimumDamage));
        const float damage = float(minimum + limitedRandom(missile.combatRandom, span)) / 256.f;
        missile.lastHit = contact->first; // LastCollide, not an all-target hit set or NextDelay.
        missile.damage = damage;
        if (!world_.avoidMissile(contact->first)) dealDamage({missile.owner, contact->first, damage, MonsterDamageType::Cold,
                    missileColdDuration(missile.owner, target, state.coldFrames)});
    } else if (!world_.pathClear(missile.missileId, missile.pos, missile.pos)) missile.remaining = 0;
}
} // namespace d2x
