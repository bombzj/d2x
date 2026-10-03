#include "gameplay/combat/damage_request.hpp"
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
#include <cstdint>
#include <stdexcept>

namespace d2x {
void SkillRuntime::launchGlacialSpike(SkillProjectileSource player, const SkillCastSpec &skill, Vec target) {
    if (!skill.freezingArea || !skill.missileImpact)
        throw std::runtime_error("Missing Glacial Spike program");
    const Vec origin{std::floor(player.pos.x) + .5f, std::floor(player.pos.y) + .5f};
    Vec heading{std::floor(target.x) - std::floor(origin.x),
                std::floor(target.y) - std::floor(origin.y)};
    if (heading.length() == 0) heading = {1, 1};
    Missile missile{world_.allocate(), player.id, origin, heading.unit() * skill.missileVelocity,
        skill.missileLifetime, skill.effect, false, skill.missileId};
    missile.combatRandom = world_.childSeed();
    missile.skillId = skill.sourceId; missile.skillRank = skill.rank;
    missile.fixedElement = MonsterDamageType::Cold;
    missile.impact = skill.missileImpact;
    missile.freezingArea.emplace();
    auto &state = *missile.freezingArea;
    state.lifetimeFrames = int(skill.missileLifetime * 25.f + .5f);
    state.minimumDamage = int(skill.minimumDamage * 256.f);
    state.maximumDamage = int(skill.maximumDamage * 256.f);
    world_.addMissile(std::move(missile));
}
void SkillRuntime::resolveGlacialSpikeImpact(Missile &missile) {
    emit(MissileImpact{missile.missileId, missile.pos});
    if (missile.impact && missile.impact->visualId >= 0)
        world_.addEffect({missile.pos, 0, missile.impact->visualDuration, missile.impact->visualId});
    if (!combatUnit(missile.owner)) return;
    if (!world_.hasResolver()) throw std::runtime_error("Glacial Spike owner has no skill resolver");
    // SrvHit13 evaluates AuraRange/AuraLen at impact with the launch rank;
    // missile damage remains the creation-time snapshot and is rolled once.
    const auto skill = world_.resolve(missile.owner, missile.skillId, missile.skillRank);
    if (!skill.freezingArea) throw std::runtime_error("Missing Glacial Spike impact formulas");
    const auto &program = *skill.freezingArea;
    const auto &state = *missile.freezingArea;
    const int minimum = std::min(state.minimumDamage, state.maximumDamage);
    const uint32_t span = uint32_t(std::abs(state.maximumDamage - state.minimumDamage));
    const float damage = float(minimum + limitedRandom(missile.combatRandom, span)) / 256.f;
    for (auto target : combatUnits()) {
        if (!target.alive() || !target.stats.resolved || !active(*target.position) || !canAttack(missile.owner, target.id)) continue;
        const int64_t dx = int(std::floor(target.position->x)) - int(std::floor(missile.pos.x));
        const int64_t dy = int(std::floor(target.position->y)) - int(std::floor(missile.pos.y));
        if (dx * dx + dy * dy > int64_t(program.radius) * program.radius) continue;
        // Aura filter 0x8583 has no LOS bit; ApplyBlockOrDodge(1,0) has
        // no shield block. Passive avoidance belongs to the shared combat model.
        DamageRequest hit{missile.owner, target.id, damage, MonsterDamageType::Cold};
        hit.freezeFrames = program.freezeFrames;
        dealDamage(hit);
    }
}
void SkillRuntime::advanceGlacialSpike(Missile &missile, std::vector<Missile> &spawned) {
    auto &state = *missile.freezingArea;
    if (state.elapsedFrames >= state.lifetimeFrames) { missile.remaining = 0; return; }
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool wall = world_.clipPath(missile.missileId, missile.pos, next);
    ++state.elapsedFrames;
    missile.age = float(state.elapsedFrames) / 25.f;
    missile.remaining = float(state.lifetimeFrames - state.elapsedFrames) / 25.f;
    // HandleMissileCollision moves, decrements, then calls SrvHit13 on expiry.
    // Unit collision also detonates at the missile's current integer coordinates.
    const bool expired = state.elapsedFrames == state.lifetimeFrames;
    const auto contact = expired ? std::nullopt : world_.missileTarget(missile, next);
    missile.pos = next;
    if (contact) reactToMissile(missile, contact->first, spawned);
    if (expired || contact || wall) {
        missile.remaining = 0;
        resolveGlacialSpikeImpact(missile);
    }
}
} // namespace d2x
