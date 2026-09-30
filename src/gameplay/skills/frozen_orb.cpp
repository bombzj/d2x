#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace d2x {
namespace {
// Native SrvDo15/SrvHit29 direction lattice, in world coordinates.
Vec orbDirection(int direction) {
    constexpr int offsets[]{30, 29, 29, 28, 27, 26, 24, 23, 21, 19, 16, 14, 11, 8, 5, 2,
        0, -2, -5, -8, -11, -14, -16, -19, -21, -23, -24, -26, -27, -28, -29, -29,
        -30, -29, -29, -28, -27, -26, -24, -23, -21, -19, -16, -14, -11, -8, -5, -2,
        0, 2, 5, 8, 11, 14, 16, 19, 21, 23, 24, 26, 27, 28, 29, 29};
    return {float(offsets[direction]), float(offsets[(direction + 48) % 64])};
}
Vec missileOrigin(Vec position) {
    // MISSILES_CreateMissileFromParams uses the origin unit's integer subcell.
    return {std::floor(position.x) + .5f, std::floor(position.y) + .5f};
}
} // namespace
void Simulation::launchFrozenOrb(PlayerState &player, const SkillCastSpec &skill, Vec target) {
    if (!skill.frozenOrb) throw std::runtime_error("Missing Frozen Orb missile program");
    const Vec origin = missileOrigin(player.pos);
    Vec heading{std::floor(target.x) - std::floor(origin.x),
                std::floor(target.y) - std::floor(origin.y)};
    if (heading.length() == 0) heading = {1, 1};
    Missile missile{ids_.allocate(), player.id, origin, heading.unit() * skill.missileVelocity,
        skill.missileLifetime, skill.effect, false, skill.missileId};
    missile.combatRandom = childRandom(unitRandom_);
    missile.skillId = skill.sourceId; missile.skillRank = skill.rank;
    missile.killOnHit = false;
    missile.frozenOrb.emplace();
    missile.frozenOrb->spec = *skill.frozenOrb;
    // TargetX is independent of the path target and starts at zero in native missile data.
    state_.area.missiles.push_back(std::move(missile));
}
void Simulation::spawnFrozenOrbBolt(const Missile &orb, Vec target, bool nova, std::vector<Missile> &spawned) {
    if (!combatUnit(orb.owner)) return;
    if (!resolveMissileSkill_) throw std::runtime_error("Frozen Orb owner has no skill resolver");
    // Each native child resolves the current owner's stats using the originating
    // skill rank. The parent has no Skill/MissileSkill damage to inherit.
    const auto skill = resolveMissileSkill_(orb.owner, orb.skillId, orb.skillRank);
    const auto &program = orb.frozenOrb->spec;
    const auto &motion = nova ? program.nova : program.bolt;
    Missile child{ids_.allocate(), orb.owner, missileOrigin(orb.pos), target.unit() * motion.speed,
        float(motion.lifetimeFrames) / 25.f, SkillBehavior::FrozenOrb, false, motion.missileId};
    child.combatRandom = childRandom(unitRandom_);
    child.skillId = orb.skillId; child.skillRank = orb.skillRank;
    child.fixedElement = MonsterDamageType::Cold;
    child.frozenOrb.emplace();
    auto &state = *child.frozenOrb;
    state.phase = nova ? FrozenOrbMissileState::Phase::Nova : FrozenOrbMissileState::Phase::Bolt;
    state.spec = program;
    state.novaTarget = target;
    state.minimumDamage = int(skill.minimumDamage * 256.f);
    state.maximumDamage = int(skill.maximumDamage * 256.f);
    state.coldFrames = int(skill.coldDuration * 25.f + .5f);
    emit(MissileReleased{child.missileId});
    spawned.push_back(std::move(child));
}
float Simulation::frozenOrbColdDuration(EntityId attacker, const CombatUnit &target, int frames) const {
    const auto &mods = target.stats.attributes.combat;
    if (frames <= 0 || mods.cannotBeFrozen) return 0;
    // Total damage processing halves the duration before resistance rounding.
    if (mods.halfFreezeDuration) frames /= 2;
    int resistance = unitResistance(target, MonsterDamageType::Cold);
    if (resistance >= 100) return 0;
    if (target.stats.monsterResistanceRules && coldPierce_) resistance -= coldPierce_(attacker);
    frames = int(int64_t(frames) * (100 - std::clamp(resistance, -100, 100)) / 100);
    if (frames <= 0) return 0;
    const int coldEffect = unitColdEffect_ ? unitColdEffect_(target) : 0;
    if (coldEffect == 0) return 0;
    if ((target.monster || target.hireling) && coldEffect < 0) frames /= monsterColdDivisor_;
    return float(std::max(1, frames)) / 25.f;
}
void Simulation::advanceFrozenOrb(Missile &missile, std::vector<Missile> &spawned) {
    auto &state = *missile.frozenOrb;
    const auto &program = state.spec;
    const bool orb = state.phase == FrozenOrbMissileState::Phase::Orb;
    const bool nova = state.phase == FrozenOrbMissileState::Phase::Nova;
    const int lifetime = orb ? program.lifetimeFrames :
        nova ? program.nova.lifetimeFrames : program.bolt.lifetimeFrames;
    if (state.elapsedFrames >= lifetime || !combatUnit(missile.owner)) { missile.remaining = 0; return; }
    // SrvDo15 emits at the pre-move position, including elapsed frame zero.
    if (orb && state.elapsedFrames % program.emissionPeriod == 0) {
        spawnFrozenOrbBolt(missile, orbDirection(state.emissionDirection), false, spawned);
        state.emissionDirection = (state.emissionDirection + program.directionStep) % 64;
    }
    if (nova && state.elapsedFrames < program.novaTurnFrames &&
        state.elapsedFrames % program.novaTurnPeriod == 0) {
        // SrvDo16 changes the path at elapsed 0, 2, 4 in the current MPQ.
        // Integer truncation is part of the native rotation/contraction.
        const int x = int(state.novaTarget.x), y = int(state.novaTarget.y);
        state.novaTarget = {float((x - y) / 2), float((x + y) / 2)};
        const Vec target = missileOrigin(missile.pos) + state.novaTarget;
        missile.velocity = (target - missile.pos).unit() * program.nova.speed;
    }
    Vec next = missile.pos + missile.velocity * (1.f / 25.f);
    const bool blocked = clipMissilePath(missile.missileId, missile.pos, next);
    ++state.elapsedFrames;
    missile.age = float(state.elapsedFrames) / 25.f;
    missile.remaining = float(std::max(0, lifetime - state.elapsedFrames)) / 25.f;
    if (orb) {
        missile.pos = next;
        // A wall does not set the countdown to zero in SrvHit29, so it does
        // not create the terminal nova. Unit contact keeps the parent moving.
        if (blocked) { missile.remaining = 0; return; }
        if (state.elapsedFrames == lifetime) {
            for (int direction = 0; direction < 64; direction += program.burstStep)
                spawnFrozenOrbBolt(missile, orbDirection(direction), true, spawned);
            emit(MissileImpact{missile.missileId, missile.pos});
        }
        return;
    }
    // HandleMissileCollision moves, then decrements, then resolves expiry
    // before searching for units on the last frame.
    if (state.elapsedFrames == lifetime) { missile.pos = next; return; }
    const auto contact = missileTarget(missile, next);
    missile.pos = contact ? missile.pos + (next - missile.pos) * contact->second : next;
    if (contact) {
        const auto target = combatUnit(contact->first);
        const int minimum = std::min(state.minimumDamage, state.maximumDamage);
        const uint32_t span = uint32_t(std::abs(state.maximumDamage - state.minimumDamage));
        const float damage = float(minimum + limitedRandom(missile.combatRandom, span)) / 256.f;
        missile.damage = damage;
        dealDamage({missile.owner, contact->first, damage, MonsterDamageType::Cold,
                    frozenOrbColdDuration(missile.owner, target, state.coldFrames)});
        emit(MissileImpact{missile.missileId, missile.pos});
        missile.lastHit = contact->first;
        missile.remaining = 0;
    } else if (blocked) missile.remaining = 0;
}
} // namespace d2x
