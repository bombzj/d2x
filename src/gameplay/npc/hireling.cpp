#include "gameplay/simulation/simulation.hpp"
#include "core/random.hpp"
#include "gameplay/session/session.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
bool GameSession::assignKashyaHireling() {
    if (state().player.hireling.sourceRow >= 0) return true;
    for (const auto &npc : region().objects) {
        if (npc.npcClass != "kashya" || !ensureHirelingOffers(npc.id)) continue;
        auto &offers = hirelingOffers_.at(npc.id);
        assignHireling(offers.front());
        offers.erase(offers.begin());
        return true;
    }
    return false;
}
void GameSession::advanceHireling(float dt) {
    auto &merc = simulation_->state_.player.hireling;
    auto &player = simulation_->state_.player;
    if (merc.sourceRow < 0 || dt <= 0) return;
    if (!merc.active()) { merc.deathAge += dt; return; }
    merc.combatEffects.expire(state().frame);
    const auto *definition = hirelingDefinition();
    const MonsterRecord *actor = nullptr;
    for (const auto &[id, entry] : monsterContent_.monsters())
        if (entry.index == merc.classId) { actor = &entry; break; }
    if (!actor || !definition || !actor->walkVelocity) return;
    merc.collisionSize = actor->collisionSize;
    if (const auto *motion = monsterContent_.hirelingMotion(merc.classId, "gh"); actor->getHitMode && motion)
        merc.baseHitDuration = motion->duration;
    if (const auto *motion = monsterContent_.hirelingMotion(merc.classId, "dt")) merc.deathDuration = motion->duration;
    const auto stats = hirelingStats();
    merc.hp = std::min(merc.hp, float(stats.base.life));
    merc.hitTime = std::max(0.f, merc.hitTime - dt);
    merc.chill = std::max(0.f, merc.chill - dt);
    merc.webSlowRemaining = std::max(0.f, merc.webSlowRemaining - dt);
    const auto base = deriveHirelingStats(*definition, merc.level);
    const int regen = base.life * 256 / 2000 + stats.combat.replenishLife;
    float lifeDelta = float(regen) / 256.f * 25.f * dt;
    float healingTime = dt;
    while (!merc.healing.empty() && healingTime > 0) {
        auto &heal = merc.healing.front();
        const float amount = std::min(heal.remaining, healingTime * heal.rate);
        healingTime -= amount / heal.rate;
        heal.remaining -= amount;
        lifeDelta += amount;
        if (heal.remaining <= .0001f) merc.healing.pop_front();
    }
    merc.hp = std::min(float(stats.base.life), merc.hp + lifeDelta);
    if (merc.hp >= stats.base.life) merc.healing.clear();
    if (merc.hitTime > 0) { merc.moving = false; return; }
    const auto *timing = monsterContent_.hirelingAttackTiming(merc.classId);
    if (merc.attack) {
        auto &attack = *merc.attack;
        ++attack.ticks;
        merc.attackTimer = std::max(0.f, float(attack.timing.durationTicks() - attack.ticks) / 25.f);
        if (!attack.released && attack.ticks >= attack.timing.actionTick()) {
            attack.released = true;
            if (!region().definition.safe && actor->attack1Projectile) {
                const auto &projectile = *actor->attack1Projectile;
                if (auto target = simulation_->combatUnit(attack.target); target.alive()) attack.aim = *target.position;
                merc.look = (attack.aim - merc.pos).unit();
                const auto &weapon = stats.weapon;
                const int spread = std::max(0, weapon.projectileMaximum - weapon.projectileMinimum);
                const int raw = weapon.projectileMinimum + int(limitedRandom(merc.combatRandom, unsigned(spread)));
                Missile missile{ids_.allocate(), merc.id, merc.pos, merc.look * projectile.velocity,
                    projectile.lifetime, SkillBehavior::None, true, projectile.id,
                    float(int64_t(raw) * projectile.sourceDamage / 128) / 256.f};
                missile.attackElements = simulation_->rollAttackElements(weapon.item, &stats.combat, nullptr, &merc.combatRandom);
                missile.attackElements.ranged = true;
                missile.weaponAttack = true;
                missile.attackElements.attackerLevel = merc.level;
                missile.attackElements.manaLeech = 0;
                missile.attackerLevel = merc.level;
                missile.attackRating = weapon.attackRating;
                missile.baseAttackRating = weapon.baseAttackRating;
                missile.attackRatingPercent = weapon.attackRatingPercent;
                missile.targetModifiers = weapon.target;
                missile.physicalDamagePercent = weapon.projectileDamagePercent;
                missile.combatRandom = childRandom(simulation_->unitRandom_);
                simulation_->state_.area.missiles.push_back(std::move(missile));
                simulation_->emit(MissileReleased{projectile.id});
            }
        }
        if (attack.ticks >= attack.timing.durationTicks()) { merc.attack.reset(); merc.attackTimer = 0; }
        return;
    }
    auto open = [&](Vec position) {
        if (!map().grid.walkable(position, actor->movementRule())) return false;
        if (missileDistance(position, player.pos) < 2 &&
            (position - player.pos).length() <= (merc.pos - player.pos).length()) return false;
        for (const auto &enemy : state().area.enemies)
            if (enemy.hp > 0 && missileDistance(position, enemy.pos) < 2 &&
                (position - enemy.pos).length() <= (merc.pos - enemy.pos).length()) return false;
        return true;
    };
    auto around = [&](Vec center, int radius, Vec away) -> std::optional<Vec> {
        constexpr Vec offsets[]{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
        std::optional<Vec> best;
        float score = -1e30f;
        for (auto offset : offsets) {
            const Vec position{std::floor(center.x + offset.x * radius) + .5f,
                               std::floor(center.y + offset.y * radius) + .5f};
            if (!open(position)) continue;
            const float candidate = (position - away).length();
            if (candidate > score) { score = candidate; best = position; }
        }
        return best;
    };
    const int ownerDistance = std::max(0, missileDistance(merc.pos, player.pos) - 2);
    // AiThink Fn061: catch up before acquiring enemies; teleport only beyond 100.
    if (ownerDistance > 100) {
        if (auto position = around(player.pos, 4, merc.pos)) {
            merc.pos = *position; merc.route.clear(); merc.moving = false; merc.thinkTimer = 5.f / 25.f;
        }
        return;
    }
    const bool hurry = ownerDistance > 24 || (ownerDistance > 16 && player.moving);
    merc.thinkTimer = std::max(0.f, merc.thinkTimer - dt);
    if (hurry && (merc.route.empty() || (merc.route.back() - player.pos).length() > 16)) {
        if (auto position = around(player.pos, 4, merc.pos)) merc.route = map().grid.path(merc.pos, *position, false, actor->movementRule());
    }
    if (!merc.route.empty()) {
        while (!merc.route.empty() && (merc.route.front() - merc.pos).length() < .01f) merc.route.pop_front();
        if (!merc.route.empty()) {
            const auto offset = merc.route.front() - merc.pos;
            const int frw = std::max(0, stats.fasterMoveVelocity);
            const bool boosted = hurry && (!player.moving || player.runningNow);
            int rate = 75 + (boosted ? 60 : 0) + 150 * frw / (150 + frw) + stats.velocityPercent;
            if (merc.webSlowRemaining > 0) rate += merc.webSlowPercent;
            rate = monsterMovementPercent(*actor, state().population.difficulty, rate, merc.chill > 0);
            const float speed = float((*actor->walkVelocity << 8) * rate / 100) * 25.f / 4096.f;
            const Vec next = merc.pos + offset.unit() * std::min(offset.length(), speed * dt);
            if (map().grid.segment(merc.pos, next, {}, actor->movementRule()) && open(next)) {
                merc.look = offset.unit(); merc.pos = next;
                if (!merc.moving) merc.animationTime = 0;
                merc.moving = true;
                merc.animationRate = actor->walkAnimationRate.value_or(0) * float(rate) / 100.f * 25.f / 256.f;
                merc.animationTime += dt * merc.animationRate;
                return;
            }
            merc.route.clear(); merc.thinkTimer = 5.f / 25.f;
        }
    }
    if (merc.moving) merc.animationTime = 0;
    merc.moving = false;
    if (const auto *idle = monsterContent_.hirelingMotion(merc.classId, "nu"))
        merc.animationRate = idle->frames / idle->duration;
    merc.animationTime += dt * merc.animationRate;
    if (merc.thinkTimer > 0) return;
    merc.thinkTimer = 5.f / 25.f;
    if (hurry) return;
    CombatUnit target;
    int closest = 25;
    if (!region().definition.safe && actor->attack1Projectile && timing)
        for (auto candidate : simulation_->combatUnits()) {
            const int distance = std::max(0, missileDistance(merc.pos, *candidate.position) - 2);
            if (candidate.alive() && simulation_->canAttack(merc.id, candidate.id) && simulation_->active(*candidate.position) && distance < closest &&
                simulation_->missilePathClear(actor->attack1Projectile->id, merc.pos, *candidate.position)) {
                closest = distance; target = candidate;
            }
        }
    if (target) {
        const int chance = std::min(merc.attackBias + 40 + 2 * merc.level, 95);
        const bool attackNow = limitedRandom(merc.combatRandom, 100) < unsigned(chance);
        merc.attackBias = attackNow ? 0 : merc.attackBias + 10;
        if (closest < 4 && limitedRandom(merc.combatRandom, 100) < 50) {
            if (ownerDistance > 4)
                if (auto position = around(player.pos, 4, *target.position)) merc.route = map().grid.path(merc.pos, *position, false, actor->movementRule());
            if (merc.route.empty())
                if (auto position = around(merc.pos, 4, *target.position)) merc.route = map().grid.path(merc.pos, *position, false, actor->movementRule());
            if (!merc.route.empty()) return;
        }
        if (!attackNow) { merc.thinkTimer = 10.f / 25.f; return; }
        const int baseRate = int(std::lround(timing->frames * 256.f / (timing->duration * 25.f)));
        const int actionFrame = int(std::lround(timing->impact * baseRate * 25.f / 256.f));
        const int cold = merc.chill > 0 ? actor->coldEffect.at(size_t(state().population.difficulty)) : 0;
        const int speed = effectiveAttackSpeed(baseRate, stats.weapon.fasterAttack,
                                               stats.weapon.baseSpeed, cold + stats.combat.attackRate);
        WeaponAttackState attack;
        attack.weapon = stats.weapon.item; attack.target = target.id; attack.aim = *target.position;
        attack.timing = {"a1", timing->frames, speed, actionFrame, 0};
        merc.attackTimer = float(attack.timing.durationTicks()) / 25.f;
        merc.attack = std::move(attack);
        merc.look = (*target.position - merc.pos).unit();
        return;
    }
    if (ownerDistance <= 1 || limitedRandom(merc.combatRandom, 100) < 5)
        if (auto position = around(player.pos, 4, merc.pos)) merc.route = map().grid.path(merc.pos, *position, false, actor->movementRule());
}
} // namespace d2x
