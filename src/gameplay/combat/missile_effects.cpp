#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x {
void Simulation::resolveMissileImpact(const Missile &missile, std::vector<Missile> &spawned, Enemy *direct) {
    if (!missile.impact) return;
    auto spec = *missile.impact;
    if (missile.skillId >= 0 && (spec.cloudBurst || spec.areaMissile)) {
        // A native child inherits the parent's rank, then resolves its damage
        // from the current owner. The parent projectile retains its launch snapshot.
        if (!resolveMissileSkill_ || missile.owner != state_.player.id)
            throw std::runtime_error("Missile owner has no skill resolver");
        const auto skill = resolveMissileSkill_(missile.skillId, missile.skillRank);
        if (!skill.missileImpact) throw std::runtime_error("Missing originating missile impact");
        spec = *skill.missileImpact;
    }
    const auto &payload = missile.impactDamage;
    emit(MissileImpact{missile.missileId, missile.pos});
    if (spec.visualId >= 0)
        state_.area.effects.push_back({missile.pos, 0, spec.visualDuration, spec.visualId});
    auto hit = [&](Enemy &enemy) {
        if (enemy.hp <= 0 || !active(enemy.pos)) return;
        float total = 0, chill = 0;
        for (size_t channel = 0; channel < size_t(MonsterDamageType::Poison); ++channel) {
            if (payload.channels[channel] <= 0) continue;
            const auto type = MonsterDamageType(channel);
            const auto resistance = monsterResistance_ ? monsterResistance_(enemy, state_.area.region, type) : std::nullopt;
            if (!resistance) {
                state_.message = "Original monster resistance data is unavailable.";
                return;
            }
            const int pierce = type == MonsterDamageType::Cold && missile.owner == state_.player.id &&
                               *resistance < 100 && coldPierce_ ? coldPierce_() : 0;
            const int effective = *resistance - pierce;
            total += mitigateMonsterDamage(payload.channels[channel], effective);
            if (type == MonsterDamageType::Cold)
                chill = payload.coldDuration * float(std::clamp(100 - effective, 0, 200)) / 100.f;
        }
        // Combine channels after independent resistance calculations: one hit reaction/kill.
        damageEnemy(enemy, total, missile.owner, chill, false, MonsterDamageType::Physical,
                    true, true, payload.freeze);
        if (enemy.hp > 0 && payload.poisonDuration > 0)
            applyEnemyPoison(enemy, payload.channels[size_t(MonsterDamageType::Poison)],
                             payload.poisonDuration, missile.owner, true);
    };
    if (spec.radius > 0) {
        // SrvHit01/44 -> sub_6FD10200 uses filter 0x8583. It does not include
        // sub_6FD0FA00's 0x200 line-of-sight test; only flight stops at barriers.
        for (auto &enemy : state_.area.enemies) {
            const float dx = std::floor(enemy.pos.x) - std::floor(missile.pos.x);
            const float dy = std::floor(enemy.pos.y) - std::floor(missile.pos.y);
            if (dx * dx + dy * dy <= spec.radius * spec.radius) hit(enemy);
        }
    } else if (direct) hit(*direct);
    if (spec.areaMissile) {
        const auto &area = *spec.areaMissile;
        int64_t minimum = area.minimum, maximum = area.maximum;
        if (area.addEquipmentElement) {
            // Native child creation reads the owner's equipment at impact, not
            // the weapon carried by the original arrow. Other owners need their own resolver.
            if (missile.owner != state_.player.id) return;
            const auto ranges = attackElementRanges(characterStats_.combat, equipmentStats_.weapons[0].item);
            const AttackDamageRange channels[]{ {}, ranges.magic, ranges.fire, ranges.lightning, ranges.cold, {} };
            minimum += int64_t(channels[size_t(area.element)].minimum) * 256;
            maximum += int64_t(channels[size_t(area.element)].maximum) * 256;
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
        spawned.push_back(std::move(child));
    }
    if (!spec.cloudBurst) return;
    // MISSMODE_CreatePoisonCloudHitSubmissiles: fixed 16-direction offsets,
    // with two independently selected rings, velocities and loop count.
    constexpr Vec offsets[16]{{0,2},{1,2},{2,2},{2,1},{2,0},{2,-1},{2,-2},{1,-2},
                              {0,-2},{-1,-2},{-2,-2},{-2,-1},{-2,0},{-2,1},{-2,2},{-1,2}};
    const auto &burst = *spec.cloudBurst;
    const auto &cloud = burst.cloud;
    const Vec origin = direct ? direct->pos : missile.pos;
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
void Simulation::advancePoisonCloud(Missile &missile, float dt) {
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
    Enemy *struck = nullptr;
    float first = 2;
    for (auto &enemy : state_.area.enemies) {
        if (enemy.hp <= 0 || !active(enemy.pos) || enemy.id == missile.lastHit) continue;
        const int size = monsterSize_ ? monsterSize_(enemy) : 0;
        if (auto at = missileUnitIntersection(missile.pos, next, cloud.size, enemy.pos, size); at && *at < first) {
            first = *at;
            struck = &enemy;
        }
    }
    if (struck) {
        missile.lastHit = struck->id; // Native LastCollide suppresses the previous unit only.
        rollRandom(missile.combatRandom);
        const auto span = uint32_t(std::max(0, cloud.maximum - cloud.minimum));
        const int rate = cloud.minimum + (span ? uint32_t(missile.combatRandom) % span : 0);
        applyEnemyPoison(*struck, float(rate) * 25.f / 256.f, float(cloud.poisonFrames) / 25.f, missile.owner, true);
    }
    missile.pos = next;
    if (blocked) missile.remaining = 0;
}
} // namespace d2x
