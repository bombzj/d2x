#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace d2x {
void Simulation::launchGlacialSpike(PlayerState &player, const SkillCastSpec &skill, Vec target) {
    if (!skill.freezingArea || !skill.missileImpact)
        throw std::runtime_error("Missing Glacial Spike program");
    const Vec origin{std::floor(player.pos.x) + .5f, std::floor(player.pos.y) + .5f};
    Vec heading{std::floor(target.x) - std::floor(origin.x),
                std::floor(target.y) - std::floor(origin.y)};
    if (heading.length() == 0) heading = {1, 1};
    Missile missile{ids_.allocate(), player.id, origin, heading.unit() * skill.missileVelocity,
        skill.missileLifetime, skill.effect, false, skill.missileId};
    missile.combatRandom = childRandom(unitRandom_);
    missile.skillId = skill.sourceId; missile.skillRank = skill.rank;
    missile.fixedElement = MonsterDamageType::Cold;
    missile.impact = skill.missileImpact;
    missile.freezingArea.emplace();
    auto &state = *missile.freezingArea;
    state.lifetimeFrames = int(skill.missileLifetime * 25.f + .5f);
    state.minimumDamage = int(skill.minimumDamage * 256.f);
    state.maximumDamage = int(skill.maximumDamage * 256.f);
    state_.area.missiles.push_back(std::move(missile));
}
void Simulation::applyMissileFreeze(EntityId attacker, CombatUnit target, int frames) {
    if (!target.alive() || frames <= 0 ||
        (target.monster && target.effects->hasState(uninterruptableState_, state_.frame))) return;
    const auto rank = target.stats.rank;
    // ApplyFreezeState turns freeze into cold slow for players, hirelings and
    // boss/unique/champion monsters. Ordinary nonnegative ColdEffect gets neither.
    if (!target.monster || target.stats.boss || rank == MonsterRank::Boss ||
        rank == MonsterRank::Unique || rank == MonsterRank::SuperUnique || rank == MonsterRank::Champion) {
        applyChill(target.id, missileColdDuration(attacker, target, frames));
        return;
    }
    const auto &mods = target.stats.attributes.combat;
    if (frames <= 0 || mods.cannotBeFrozen || !target.stats.freezable ||
        !unitColdEffect_ || unitColdEffect_(target) >= 0) return;
    if (mods.halfFreezeDuration) frames /= 2;
    int resistance = unitResistance(target, MonsterDamageType::Cold);
    if (resistance >= 100) return;
    if (target.stats.monsterResistanceRules && coldPierce_) resistance -= coldPierce_(attacker);
    frames = int(int64_t(frames) * (100 - std::clamp(resistance, -100, 100)) / 100);
    if (frames <= 0) return;
    // FreezeDiv is distinct from ColdDiv, with integer truncation and no one-frame floor.
    // The state bit is installed even if division truncates the duration to zero;
    // a lethal hit can still preserve it before the next remove-state tick.
    target.monster->freezeActive = true;
    frames /= monsterFreezeDivisor_;
    target.monster->freeze = std::max(target.monster->freeze, float(frames) / 25.f);
    if (frames > 0) target.monster->route.clear();
}
void Simulation::resolveGlacialSpikeImpact(Missile &missile) {
    emit(MissileImpact{missile.missileId, missile.pos});
    if (missile.impact && missile.impact->visualId >= 0)
        state_.area.effects.push_back({missile.pos, 0, missile.impact->visualDuration, missile.impact->visualId});
    if (!combatUnit(missile.owner)) return;
    if (!resolveMissileSkill_) throw std::runtime_error("Glacial Spike owner has no skill resolver");
    // SrvHit13 evaluates AuraRange/AuraLen at impact with the launch rank;
    // missile damage remains the creation-time snapshot and is rolled once.
    const auto skill = resolveMissileSkill_(missile.owner, missile.skillId, missile.skillRank);
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
void Simulation::advanceGlacialSpike(Missile &missile, std::vector<Missile> &spawned) {
    auto &state = *missile.freezingArea;
    if (state.elapsedFrames >= state.lifetimeFrames) { missile.remaining = 0; return; }
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool wall = clipMissilePath(missile.missileId, missile.pos, next);
    ++state.elapsedFrames;
    missile.age = float(state.elapsedFrames) / 25.f;
    missile.remaining = float(state.lifetimeFrames - state.elapsedFrames) / 25.f;
    // HandleMissileCollision moves, decrements, then calls SrvHit13 on expiry.
    // Unit collision also detonates at the missile's current integer coordinates.
    const bool expired = state.elapsedFrames == state.lifetimeFrames;
    const auto contact = expired ? std::nullopt : missileTarget(missile, next);
    missile.pos = next;
    if (contact) reactToMissile(missile, contact->first, spawned);
    if (expired || contact || wall) {
        missile.remaining = 0;
        resolveGlacialSpikeImpact(missile);
    }
}
} // namespace d2x
