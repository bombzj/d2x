#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/effects/state.hpp"
#include "gameplay/skills/caster.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/runtime.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void SkillRuntime::advanceFirewall(Missile &missile, std::vector<Missile> &spawned) {
    auto &firewall = *missile.firewall;
    const auto &definition = firewall.definition;
    if (++firewall.elapsedFrames >= (firewall.maker ? definition.makerFrames : definition.fireFrames)) {
        missile.remaining = 0;
        return;
    }
    missile.age += 1.f / 25.f;
    missile.remaining = float((firewall.maker ? definition.makerFrames : definition.fireFrames) - firewall.elapsedFrames) / 25.f;
    if (firewall.maker) {
        Vec next = missile.pos + missile.velocity * (1.f / 25.f);
        if (world_.clipPath(missile.missileId, missile.pos, next)) { missile.remaining = 0; return; }
        if (int(next.x) != int(missile.pos.x) || int(next.y) != int(missile.pos.y)) {
            Missile fire{world_.allocate(), missile.owner, {std::floor(next.x) + .5f, std::floor(next.y) + .5f}, {},
                float(definition.fireFrames) / 25.f, missile.behavior, false, definition.fireId};
            fire.firewall = Missile::FirewallState{definition, false, 0};
            fire.combatRandom = world_.childSeed();
            spawned.push_back(std::move(fire));
        }
        missile.pos = next;
        return;
    }
    for (auto target : combatUnits())
        if (target.alive() && canAttack(missile.owner, target.id) &&
            missileUnitIntersection(missile.pos, missile.pos, definition.size, *target.position, target.stats.collisionSize)) {
            rollRandom(missile.combatRandom);
            const int damage = definition.minimumDamage + int(uint32_t(missile.combatRandom) %
                unsigned(definition.maximumDamage - definition.minimumDamage + 1));
            DamageRequest hit{missile.owner, target.id, float(damage << definition.hitShift) / 256.f, MonsterDamageType::Fire};
            if (missile.behavior == SkillBehavior::FireWall || missile.behavior == SkillBehavior::Blaze ||
                missile.behavior == SkillBehavior::Meteor) {
                reactToMissile(missile, target.id, spawned);
                rollRandom(missile.combatRandom);
                hit.softHit = int(uint32_t(missile.combatRandom) & 127) < definition.softHitChance;
                hit.hitRecovery = false;
            }
            dealDamage(hit);
        }
}
void SkillRuntime::createBlazeTrail(SkillCaster player) {
    if (world_.safeZone() || !player.moving || !world_.hasResolver() ||
        (int(player.previous.x) == int(player.pos.x) && int(player.previous.y) == int(player.pos.y))) return;
    for (const auto &effect : player.combatEffects.entries()) {
        if (effect.spec.state.id != world_.blazeState() || !effect.activeAt(world_.frame()) ||
            effect.spec.source.kind != CombatEffectSource::Skill) continue;
        const auto skill = world_.resolve(player.id, effect.spec.source.definition, effect.spec.source.level);
        if (skill.effect != SkillBehavior::Blaze || !skill.firewall) continue;
        const auto &definition = *skill.firewall;
        Missile fire{world_.allocate(), player.id, {float(int(player.pos.x)), float(int(player.pos.y))}, {},
            float(definition.fireFrames) / 25.f, SkillBehavior::Blaze, false, definition.fireId};
        fire.firewall = Missile::FirewallState{definition, false, 0};
        fire.combatRandom = world_.childSeed();
        world_.addMissile(std::move(fire));
    }
}
void SkillRuntime::advanceThunderStorm(SkillCaster player) {
    if (!player.thunderStorm || !world_.hasResolver()) return;
    const auto &effects = player.combatEffects.entries();
    const auto effect = std::find_if(effects.begin(), effects.end(), [&](const auto &entry) {
        return entry.handle == player.thunderStorm->effect && entry.activeAt(world_.frame());
    });
    if (player.dead || effect == effects.end()) { player.thunderStorm.reset(); return; }
    if (world_.frame() < player.thunderStorm->nextFrame) return;
    const auto skill = world_.resolve(player.id, effect->spec.source.definition, effect->spec.source.level);
    const auto period = EffectFrame(skill.stormPeriod);
    player.thunderStorm->nextFrame = ((world_.frame() + period - 1) / period) * period + 1;
    if (world_.safeZone()) return;
    EntityId next, fallback;
    for (const auto &target : combatUnits()) {
        if (!target.alive() || !target.identity.attackable || !canAttack(player.id, target.id) ||
            !active(*target.position)) continue;
        const int deltaX = int(target.position->x) - int(player.pos.x);
        const int deltaY = int(target.position->y) - int(player.pos.y);
        if (deltaX * deltaX + deltaY * deltaY > skill.stormRadius * skill.stormRadius ||
            !world_.missileSegment(player.pos, *target.position, {0x04, 1})) continue;
        if (!fallback || target.id < fallback) fallback = target.id;
        if (target.id > player.thunderStorm->lastTarget && (!next || target.id < next)) next = target.id;
    }
    if (!next) next = fallback;
    player.thunderStorm->lastTarget = next;
    if (!next) return;
    const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
    const float amount = float(minimum + limitedRandom(player.combatRandom,
        unsigned(std::max(0, maximum - minimum)))) / 256.f;
    Missile missile{world_.allocate(), player.id, unitPosition(next), {}, skill.missileLifetime,
        SkillBehavior::ThunderStorm, false, skill.missileId};
    missile.combatRandom = world_.childSeed();
    std::vector<Missile> spawned;
    reactToMissile(missile, next, spawned);
    dealDamage({player.id, next, amount, MonsterDamageType::Lightning});
    for (auto &child : spawned) world_.addMissile(std::move(child));
    world_.addEffect({missile.pos, 0, skill.missileLifetime, skill.missileId});
    emit(MissileReleased{skill.missileId});
}
} // namespace d2x
