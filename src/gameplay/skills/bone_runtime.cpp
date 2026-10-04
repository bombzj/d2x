#include "runtime.hpp"
#include "behavior.hpp"
#include "bone_spec.hpp"
#include "cast_spec.hpp"
#include "caster.hpp"
#include "world_port.hpp"
#include "world_values.hpp"
#include "missile.hpp"
#include "gameplay/combat/unit.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
bool SkillRuntime::releaseBoneWall(SkillCaster actor, const SkillCastSpec &skill, Vec target) {
    if (skill.bone->prison) {
        constexpr Vec offsets[]{{-1,-4},{1,-4},{3,-3},{4,-1},{4,1},{3,3},
            {-1,4},{1,4},{-3,3},{-4,-1},{-4,1},{-3,-3}};
        EntityId root;
        const Vec center{float(int(target.x)) + .5f, float(int(target.y)) + .5f};
        for (const auto offset : offsets) {
            const auto id = world_.createBoneBarrier(actor.id, *skill.bone, center + offset, root,
                skill.sourceId, skill.rank, true, Vec{-offset.x, -offset.y});
            if (!root && id) root = id;
        }
        return true; // Native SrvDo062 succeeds even if all twelve placements fail.
    }
    const auto root = world_.createBoneBarrier(actor.id, *skill.bone, target, {}, skill.sourceId, skill.rank, true);
    if (!root) return false;
    const auto center = unitPosition(root);
    Vec delta{float(int(actor.pos.x) - int(center.x)), float(int(actor.pos.y) - int(center.y))};
    if (delta.length() == 0) delta.x = 1;
    const Vec direction = Vec{-delta.y, delta.x}.unit();
    if (skill.bone->sideCount > 1)
        for (int side : {-1, 1}) {
            Missile maker{world_.allocate(), actor.id, center, direction * (float(side) * skill.missileVelocity),
                skill.missileLifetime, skill.effect, false, skill.missileId};
            maker.skillId = skill.sourceId; maker.skillRank = skill.rank;
            maker.bone = std::make_shared<BoneMissileState>(BoneMissileState{skill.bone, root, skill.bone->sideCount});
            world_.addMissile(std::move(maker));
        }
    return true;
}
bool SkillRuntime::advanceBoneMissile(Missile &missile, float dt) {
    auto &state = *missile.bone;
    if (state.program->spirit) {
        if (!combatUnit(missile.owner).alive() || world_.safeZone()) { missile.remaining = 0; return true; }
        if (missile.remaining <= dt + .00001f) {
            if (state.coordinateTarget && !state.searched) {
                state.searched = true;
                for (auto unit : combatUnits()) {
                    if (!unit.alive() || !active(*unit.position) || !canAttack(missile.owner, unit.id) ||
                        relation(missile.owner, unit.id) != Relation::Hostile) continue;
                    const int dx = int(unit.position->x) - int(missile.pos.x);
                    const int dy = int(unit.position->y) - int(missile.pos.y);
                    if (dx * dx + dy * dy <= state.program->searchRadius * state.program->searchRadius &&
                        world_.pathClear(missile.missileId, missile.pos, *unit.position) &&
                        (!state.target || unit.id < state.target)) state.target = unit.id;
                }
                missile.remaining = state.program->spiritLifetime;
                const auto target = combatUnit(state.target);
                const auto heading = target.alive() ? (*target.position - missile.pos).unit() : state.offset.unit();
                if (heading.length() > 0) missile.velocity = heading * missile.velocity.length();
            } else {
                std::vector<Missile> children;
                world_.impact(missile, children, {});
                for (auto &child : children) world_.enqueueMissile(std::move(child));
                missile.remaining = 0; return true;
            }
        }
        const auto target = combatUnit(state.target);
        if (target.alive() && canAttack(missile.owner, target.id) &&
            int(missile.remaining * 25.f + .001f) % state.program->retargetPeriod == 0) {
            const int distance = missileDistance(missile.pos, *target.position);
            if (distance > 3 && distance < 25) {
                const auto heading = (*target.position - missile.pos).unit();
                if (heading.length() > 0) missile.velocity = heading * missile.velocity.length();
            }
        }
        return false;
    }
    if (state.program->trailId >= 0) {
        for (int i = 0; i < state.program->trailCount; ++i)
            world_.addEffect({missile.pos - missile.velocity * (dt * float(i) / float(state.program->trailCount)),
                0, state.program->trailDuration, state.program->trailId});
        return false;
    }
    if (!combatUnit(state.root).alive() || state.remaining <= 0) { missile.remaining = 0; return true; }
    const auto previous = missile.pos;
    missile.pos = missile.pos + missile.velocity * dt;
    missile.age += dt; missile.remaining -= dt;
    if (int(previous.x) != int(missile.pos.x) || int(previous.y) != int(missile.pos.y))
        if (world_.createBoneBarrier(missile.owner, *state.program, missile.pos, state.root, missile.skillId, missile.skillRank))
            --state.remaining;
    if (state.remaining <= 0) missile.remaining = 0;
    return true;
}
bool SkillRuntime::releaseCorpseExplosion(SkillCaster actor, const SkillCastSpec &skill, EntityId corpse) {
    if (!skill.bone || !world_.usableCorpse(corpse, true)) return false;
    if (skill.effect == SkillBehavior::PoisonExplosion) {
        if (!skill.missileImpact || !skill.missileImpact->cloudBurst) return false;
        Missile origin{{}, actor.id, unitPosition(corpse), {}, 0, skill.effect, false, skill.missileId};
        origin.skillId = skill.sourceId; origin.skillRank = skill.rank;
        origin.impact = skill.missileImpact;
        std::vector<Missile> spawned;
        world_.impact(origin, spawned, corpse);
        world_.consumeCorpse(corpse, false);
        for (auto &missile : spawned) world_.enqueueMissile(std::move(missile));
        return true;
    }
    const auto source = world_.corpseExplosionSource(corpse);
    if (source.maximumLife <= 0 || !source.random) return false;
    const auto &program = *skill.bone;
    const Vec center = unitPosition(corpse);
    const int64_t minimum = source.maximumLife * program.minimumPercent / 100;
    const int64_t maximum = source.maximumLife * program.maximumPercent / 100;
    int64_t damage = minimum + limitedRandom(*source.random, unsigned(std::max<int64_t>(0, maximum - minimum)));
    const int actorLevel = combatUnit(actor.id).stats.level;
    if (source.level > actorLevel) damage = damage * actorLevel / source.level;
    const int64_t fire = damage * program.elementalPercent / 100;
    const int radius = (program.radius + 1) / 2, physicalRadius = program.radius / 2;
    std::vector<EntityId> targets;
    for (auto unit : combatUnits())
        if (unit.alive() && canAttack(actor.id, unit.id)) targets.push_back(unit.id);
    world_.consumeCorpse(corpse);
    emit(MissileImpact{program.corpseVisual, center});
    if (program.corpseVisual >= 0)
        world_.addEffect({center, 0, program.corpseVisualDuration, program.corpseVisual});
    for (auto id : targets) {
        const auto unit = combatUnit(id);
        if (!unit.alive()) continue;
        const int dx = int(unit.position->x) - int(center.x), dy = int(unit.position->y) - int(center.y);
        const int distance = dx * dx + dy * dy;
        if (distance > radius * radius) continue;
        DamageRequest hit{actor.id, id};
        hit.channels[size_t(MonsterDamageType::Fire)] = float(fire) / 256.f;
        if (distance <= physicalRadius * physicalRadius)
            hit.channels[size_t(MonsterDamageType::Physical)] = float(damage - fire) / 256.f;
        dealDamage(hit);
    }
    return true;
}
}
