#include "gameplay/skills/behavior.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/world_port.hpp"
#include "gameplay/skills/bow_spec.hpp"
#include "gameplay/skills/spear_spec.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
void Simulation::resolveMissileImpact(const Missile &missile, std::vector<Missile> &spawned, EntityId direct) {
    if (!missile.impact) return;
    // Native child creation requires a surviving owner record. Hiring removes
    // the former mercenary; their remaining missiles must not resolve skills
    // against the replacement or a nonexistent source.
    if (!combatUnit(missile.owner)) return;
    if (missile.spear && missile.spear->program->kind == SpearSkillSpec::Kind::Fury) {
        skills().releaseSpearImpact(missile, spawned);
        emit(MissileImpact{missile.missileId, missile.pos});
        return;
    }
    auto spec = *missile.impact;
    float coldDuration = 0;
    if (missile.skillId >= 0 && (spec.cloudBurst || spec.areaMissile)) {
        // A native child inherits the parent's rank, then resolves its damage
        // from the current owner. The parent projectile retains its launch snapshot.
        if (!resolveUnitSkill_)
            throw std::runtime_error("Missile owner has no skill resolver");
        const auto skill = resolveUnitSkill_(missile.owner, missile.skillId, missile.skillRank);
        coldDuration = skill.coldDuration;
        if (!skill.missileImpact) throw std::runtime_error("Missing originating missile impact");
        spec = *skill.missileImpact;
    }
    const auto &payload = missile.impactDamage;
    emit(MissileImpact{missile.missileId, missile.pos});
    if (spec.visualId >= 0)
        state_.area.effects.push_back({missile.pos, 0, spec.visualDuration, spec.visualId});
    auto hit = [&](CombatUnit target) {
        if (!target.alive() || !active(*target.position) || !canAttack(missile.owner, target.id)) return;
        if (skillWorld_->avoidMissile(target.id)) return;
        DamageRequest request{missile.owner, target.id};
        request.channels = payload.channels;
        request.channels[size_t(MonsterDamageType::Poison)] = 0;
        request.chill = payload.coldDuration * float(std::clamp(100 - rawResistance(target.stats.attributes, MonsterDamageType::Cold), 0, 200)) / 100.f;
        request.freeze = payload.freeze;
        if (payload.freeze) {
            request.chill = 0;
            request.freezeFrames = int(payload.coldDuration * 25.f + .5f);
        }
        dealDamage(request);
        if (payload.poisonDuration > 0)
            applyPoison(target.id, payload.channels[size_t(MonsterDamageType::Poison)], payload.poisonDuration, missile.owner);
    };
    if (spec.radius > 0) {
        for (auto target : combatUnits()) {
            const float dx = std::floor(target.position->x) - std::floor(missile.pos.x);
            const float dy = std::floor(target.position->y) - std::floor(missile.pos.y);
            if (dx * dx + dy * dy <= spec.radius * spec.radius) hit(target);
        }
    } else if (direct) hit(combatUnit(direct));
    if (spec.areaMissile) {
        const auto &area = *spec.areaMissile;
        int64_t minimum = area.minimum, maximum = area.maximum;
        if (area.addEquipmentElement) {
            // Native child creation reads the owner's equipment at impact, not
            // the weapon carried by the original arrow. Other owners need their own resolver.
            const auto owner = combatUnit(missile.owner);
            if (!owner) return;
            const auto weapon = owner.player ? owner.records.player->equipment.weapons[0].item : EntityId{};
            const auto ranges = attackElementRanges(owner.stats.attributes.combat, weapon);
            const AttackDamageRange channels[]{ {}, ranges.magic, ranges.fire, ranges.lightning, ranges.cold, {} };
            minimum += int64_t(channels[size_t(area.element)].minimum) * 256;
            maximum += int64_t(channels[size_t(area.element)].maximum) * 256;
            if (area.element == DamageType::Cold) {
                WeaponModifiers own;
                if (auto found = owner.stats.attributes.combat.weapons.find(weapon); found != owner.stats.attributes.combat.weapons.end()) own = found->second;
                coldDuration += float(owner.stats.attributes.combat.coldFrames + own.coldFrames) / 25.f;
            }
        }
        Missile child{ids_.allocate(), missile.owner, missile.pos, {}, float(area.delayFrames) / 25.f,
            SkillBehavior::None, false, area.missileId};
        child.groundTargeted = true;
        child.impact = MissileImpactSpec{};
        child.impact->radius = area.radius;
        child.combatRandom = childRandom(unitRandom_);
        rollRandom(child.combatRandom);
        const uint64_t span = uint64_t(std::max<int64_t>(0, maximum - minimum));
        child.impactDamage.channels[size_t(area.element)] =
            float(minimum + (span ? uint32_t(child.combatRandom) % span : 0)) / 256.f;
        if (area.element == DamageType::Cold) {
            child.impactDamage.coldDuration = coldDuration; child.impactDamage.freeze = true;
        }
        spawned.push_back(std::move(child));
    }
    if (missile.skillId >= 0 && resolveUnitSkill_) {
        const auto skill = resolveUnitSkill_(missile.owner, missile.skillId, missile.skillRank);
        if (skill.weapon && skill.weapon->bow && skill.weapon->bow->immolation && !safeZone_) {
            const auto &bow = *skill.weapon->bow;
            const Vec center{float(int(missile.pos.x)),float(int(missile.pos.y))};
            for (int x = -bow.fireRadius; x <= bow.fireRadius; ++x)
                for (int y = -bow.fireRadius; y <= bow.fireRadius; ++y) {
                    const Vec point = center + Vec{float(x),float(y)};
                    if (x*x + y*y > bow.fireRadius * bow.fireRadius ||
                        !grid_->segment(point, point + Vec{float(x),float(y)}, {}, {0x04,1})) continue;
                    Missile fire{ids_.allocate(), missile.owner, point, {}, float(bow.fire.fireFrames) / 25.f,
                        SkillBehavior::WeaponProjectile, false, bow.fire.fireId};
                    fire.firewall = Missile::FirewallState{bow.fire, false, 0};
                    fire.combatRandom = childRandom(unitRandom_); spawned.push_back(std::move(fire));
                }
        }
    }
    if (!spec.cloudBurst) return;
    // MISSMODE_CreatePoisonCloudHitSubmissiles: fixed 16-direction offsets,
    // with two independently selected rings, velocities and loop count.
    constexpr Vec offsets[16]{{0,2},{1,2},{2,2},{2,1},{2,0},{2,-1},{2,-2},{1,-2},
                              {0,-2},{-1,-2},{-2,-2},{-2,-1},{-2,0},{-2,1},{-2,2},{-1,2}};
    const auto &burst = *spec.cloudBurst;
    const auto &cloud = burst.cloud;
    const Vec origin = direct ? unitPosition(direct) : missile.pos;
    auto launch = [&](Vec heading, float speed) {
        Missile next{ids_.allocate(), missile.owner, origin, heading.unit() * speed,
            float(cloud.lifetimeFrames) / 25.f, SkillBehavior::None, false, cloud.missileId};
        next.poisonCloud = cloud;
        next.combatRandom = childRandom(unitRandom_);
        spawned.push_back(std::move(next));
    };
    for (int i = 0; i < 16; i += burst.mainStep) launch(offsets[i], burst.mainSpeed);
    if (burst.subStep > 0)
        for (int i = 0; i < 15; i += burst.subStep) launch(offsets[i + 1], burst.subSpeed);
}
void Simulation::advanceGroundTargetedMissile(Missile &missile, float dt, std::vector<Missile> &spawned) {
    Vec next = missile.pos + missile.velocity * std::min(dt, missile.remaining);
    const bool blocked = clipMissilePath(missile.missileId, missile.pos, next);
    missile.pos = next;
    missile.remaining = blocked ? 0 : std::max(0.f, missile.remaining - dt);
    // CollideType 6 collides with terrain only. Resolve the impact at expiry/terrain.
    if (missile.remaining <= .00001f) {
        missile.remaining = 0;
        resolveMissileImpact(missile, spawned);
    }
}
void Simulation::advancePoisonCloud(Missile &missile, float dt, std::vector<Missile> &spawned) {
    const auto &cloud = *missile.poisonCloud;
    Vec next = missile.pos + missile.velocity * std::min(dt, missile.remaining);
    const bool blocked = clipMissilePath(missile.missileId, missile.pos, next);
    missile.remaining = std::max(0.f, missile.remaining - dt);
    // SrvDo03 -> HandleMissileCollision expires before querying units. A wall
    // farther along this step must not discard a contact on the clear prefix.
    if (missile.remaining <= .00001f ||
        (blocked && !missilePathClear(missile.missileId, missile.pos, missile.pos))) {
        missile.pos = next;
        missile.remaining = 0;
        return;
    }
    CombatUnit struck;
    float first = 2;
    for (auto target : combatUnits()) {
        if (!target.alive() || !canAttack(missile.owner, target.id) || !active(*target.position) || target.id == missile.lastHit) continue;
        if (auto at = missileUnitIntersection(missile.pos, next, cloud.size, *target.position, target.stats.collisionSize); at && *at < first) {
            first = *at; struck = target;
        }
    }
    if (struck) {
        skills().reactToMissile(missile, struck.id, spawned);
        missile.lastHit = struck.id;
        rollRandom(missile.combatRandom);
        const auto span = uint32_t(std::max(0, cloud.maximum - cloud.minimum));
        const int rate = cloud.minimum + (span ? uint32_t(missile.combatRandom) % span : 0);
        applyPoison(struck.id, float(rate) * 25.f / 256.f, float(cloud.poisonFrames) / 25.f, missile.owner);
    }
    missile.pos = next;
    if (blocked) missile.remaining = 0;
}
} // namespace d2x
