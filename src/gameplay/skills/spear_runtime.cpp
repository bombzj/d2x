#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/skills/projectile_source.hpp"
#include "gameplay/skills/projectile_path.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/cast_spec.hpp"
#include "gameplay/skills/missile.hpp"
#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/unit.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
void SkillRuntime::releaseSpearMelee(SkillProjectileSource actor, const SkillCastSpec &skill, Vec target, EntityId targetUnit) {
    const auto &program = *skill.weapon->spear;
    if (program.kind == SpearSkillSpec::Kind::Strike) {
        EntityId successor, fallback;
        for (const auto &candidate : combatUnits()) {
            if (candidate.id == targetUnit || !candidate.alive() || !active(*candidate.position) || !canAttack(actor.id, candidate.id)) continue;
            const int dx = int(candidate.position->x) - int(target.x), dy = int(candidate.position->y) - int(target.y);
            if (dx * dx + dy * dy > program.targetRadius * program.targetRadius) continue;
            if (!fallback || candidate.id < fallback) fallback = candidate.id;
            if (candidate.id > targetUnit && (!successor || candidate.id < successor)) successor = candidate.id;
        }
        if (!successor) successor = fallback;
        if (!successor) return;
        const Vec origin{float(int(target.x)) + .5f, float(int(target.y)) + .5f};
        Missile arc{world_.allocate(), actor.id, origin, (unitPosition(successor) - origin).unit() * skill.missileVelocity,
                    skill.missileLifetime, SkillBehavior::ChainLightning, false, skill.missileId};
        arc.combatRandom = world_.childSeed(); arc.skillId = skill.sourceId; arc.skillRank = skill.rank;
        arc.lastHit = targetUnit; arc.fixedElement = DamageType::Lightning;
        arc.spear = std::make_shared<SpearMissileState>(); arc.spear->program = skill.weapon->spear;
        arc.arc = Missile::ArcState{*skill.arc, skill.arc->count, int(skill.minimumDamage * 256.f), int(skill.maximumDamage * 256.f)};
        arc.hitOverlayId = skill.hitOverlayId; arc.hitOverlayDuration = skill.hitOverlayDuration;
        world_.enqueueMissile(std::move(arc)); emit(MissileReleased{skill.missileId}); return;
    }
    if (program.kind != SpearSkillSpec::Kind::Charged) return;
    const Vec origin{float(int(target.x)) + .5f, float(int(target.y)) + .5f};
    const Vec destination = target * 2.f - actor.pos;
    const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
    // SrvDo011 creates every bolt at the struck unit, including when the melee roll missed.
    for (int index = 0; index < skill.missileCount; ++index) {
        Missile bolt{world_.allocate(), actor.id, origin, (destination - origin).unit() * skill.missileVelocity,
                     skill.missileLifetime, SkillBehavior::ChargedBolt, false, skill.missileId};
        bolt.combatRandom = world_.childSeed();
        bolt.damage = float(minimum + limitedRandom(bolt.combatRandom, unsigned(std::max(0, maximum - minimum)))) / 256.f;
        bolt.fixedElement = DamageType::Lightning;
        bolt.skillId = skill.sourceId; bolt.skillRank = skill.rank;
        bolt.spear = std::make_shared<SpearMissileState>(); bolt.spear->program = skill.weapon->spear;
        const auto path = chargedBoltPath(origin, destination, index, int(skill.missileLifetime * 25.f + .5f));
        bolt.path.assign(path.begin(), path.end());
        world_.enqueueMissile(std::move(bolt));
    }
    emit(MissileReleased{skill.missileId});
}
void SkillRuntime::releaseSpearImpact(const Missile &missile, std::vector<Missile> &spawned) {
    if (!world_.hasResolver()) return;
    const auto skill = world_.resolve(missile.owner, missile.skillId, missile.skillRank);
    if (!skill.weapon || !skill.weapon->spear || skill.weapon->spear->kind != SpearSkillSpec::Kind::Fury) return;
    const auto &program = *skill.weapon->spear;
    std::vector<EntityId> targets;
    for (const auto &candidate : combatUnits()) {
        if (!candidate.alive() || !active(*candidate.position) || !canAttack(missile.owner, candidate.id)) continue;
        const int dx = int(candidate.position->x) - int(missile.pos.x), dy = int(candidate.position->y) - int(missile.pos.y);
        if (dx * dx + dy * dy <= program.targetRadius * program.targetRadius) targets.push_back(candidate.id);
    }
    // Native AuraFilter 0xA583 does not request a line-of-sight check.
    std::sort(targets.begin(), targets.end());
    const int minimum = int(skill.minimumDamage * 256.f), maximum = int(skill.maximumDamage * 256.f);
    const Vec origin{float(int(missile.pos.x)) + .5f, float(int(missile.pos.y)) + .5f};
    for (int index = 0; index < std::min(program.countBase, int(targets.size())); ++index) {
        Vec heading = (unitPosition(targets[size_t(index)]) - origin).unit();
        if (heading.length() == 0) heading = missile.velocity.unit();
        Missile child{world_.allocate(), missile.owner, origin, heading * program.childSpeed,
                      program.childLifetime, SkillBehavior::WeaponProjectile, false, program.childId};
        child.combatRandom = world_.childSeed();
        child.damage = float(minimum + limitedRandom(child.combatRandom, unsigned(std::max(0, maximum - minimum)))) / 256.f;
        child.fixedElement = DamageType::Lightning; child.killOnHit = program.childKillOnHit;
        child.skillId = skill.sourceId; child.skillRank = skill.rank;
        spawned.push_back(std::move(child));
    }
}
} // namespace d2x
