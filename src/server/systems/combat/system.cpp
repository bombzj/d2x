#include "system.hpp"
#include "server/area_store.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/damage_resolution.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
namespace d2x::server::combat {
namespace {
const PlayerState *playerActor(const PlayerStore &players, EntityId actor) {
    for (const auto &[id, player] : players.all()) { (void)id; if (player.actor == actor) return &player; }
    return nullptr;
}
}
DomainResult<> System::enqueue(const Damage &damage) {
    if (!damage.source || !damage.target || damage.source == damage.target || damage.type != DamageType::Physical ||
        damage.minimum < 0 || damage.maximum < damage.minimum || damage.maximum > INT32_MAX || !damage.action || damage.level <= 0)
        return {DomainStatus::InvalidRequest, {}};
    if (state_.pending.size() >= 4096) return {DomainStatus::Capacity, {}};
    if (std::any_of(state_.pending.begin(), state_.pending.end(), [&](const auto &existing) { return existing.source == damage.source; }))
        return {DomainStatus::Conflict, {}};
    state_.pending.push_back(damage);
    return {DomainStatus::Applied, std::monostate{}};
}
void System::cancel(EntityId source) { std::erase_if(state_.pending, [&](const auto &entry) { return entry.source == source; }); }
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked = false;
    for (auto it = state_.pending.begin(); it != state_.pending.end();) {
        const auto &damage = *it;
        if (damage.impact > tick.tick) { ++it; continue; }
        const auto *sourcePlayer = playerActor(ports_.players, damage.source), *targetPlayer = playerActor(ports_.players, damage.target);
        const auto *sourceMonster = ports_.monsters.find(damage.source), *targetMonster = ports_.monsters.find(damage.target);
        const bool playerAttack = sourcePlayer && targetMonster;
        const bool monsterAttack = sourceMonster && targetPlayer;
        const auto *area = ports_.areas.find(damage.area);
        bool valid = area && !area->definition.town && ((playerAttack && damage.weapon.has_value()) || monsterAttack);
        if (sourcePlayer) valid = valid && sourcePlayer->entered && sourcePlayer->area == damage.area && sourcePlayer->persistent.player.hp > 0;
        if (targetPlayer) valid = valid && targetPlayer->entered && targetPlayer->area == damage.area && targetPlayer->persistent.player.hp > 0;
        if (sourceMonster) valid = valid && sourceMonster->area == damage.area && sourceMonster->life > 0;
        if (targetMonster) valid = valid && targetMonster->area == damage.area && targetMonster->life > 0;
        if (!valid) { it = state_.pending.erase(it); continue; }
        const Vec from = sourcePlayer ? sourcePlayer->position : sourceMonster->position;
        const Vec to = targetPlayer ? targetPlayer->position : targetMonster->position;
        if (meleeDistance(from, damage.sourceSize, to, targetPlayer ? 2 : targetMonster->rule.size) > damage.range ||
            !area->definition.collision.segment(from, to)) { it = state_.pending.erase(it); continue; }
        // Reserve both private resource and public hit outputs before consuming RNG.
        if (!ports_.events.hasCapacity(2, targetPlayer ? 2 : 1)) { blocked = true; ++it; continue; }
        auto random = ports_.random;
        const bool running = targetPlayer && targetPlayer->moving && targetPlayer->runningNow;
        const auto chance = [&] {
            if (targetMonster && damage.weapon) {
                const auto &weapon = *damage.weapon;
                return weaponHitChance(damage.level, weapon.baseAttackRating, weapon.attackRatingPercent, weapon.target,
                    {targetMonster->rule.level, targetMonster->rule.defense, targetMonster->rule.demon, targetMonster->rule.undead, false}, targetMonster->identity.rank);
            }
            return physicalHitChance(damage.level, damage.rating, targetPlayer->persistent.player.level, targetPlayer->totals.equipment.defense);
        };
        bool hit = running || int(limitedRandom(random, 100)) < chance();
        if (hit && targetPlayer && targetPlayer->totals.equipment.shield) {
            const int block = targetPlayer->totals.equipment.blockChance / (running ? 3 : 1);
            hit = int(limitedRandom(random, 100)) >= block;
        }
        int64_t amount = 0;
        if (hit) {
            if (damage.weapon && targetMonster) {
                // Single-player meleeDamage ordering and target-specific physical bonuses.
                const auto &weapon = *damage.weapon;
                const int targetBonus = (targetMonster->rule.demon ? std::max(0, weapon.target.demonDamage) : 0) +
                    (targetMonster->rule.undead ? std::max(0, weapon.target.undeadDamage + (weapon.blunt ? 50 : 0)) : 0);
                const int64_t bonus = std::max<int64_t>(-90, int64_t(weapon.damagePercent) + targetBonus);
                const auto minimum = int64_t(weapon.meleeBaseMinimum) + int64_t(weapon.meleeBaseMinimum) * (bonus + weapon.minimumDamagePercent) / 100;
                const auto maximum = int64_t(weapon.meleeBaseMaximum) + int64_t(weapon.meleeBaseMaximum) * (bonus + weapon.maximumDamagePercent) / 100;
                const auto low = std::clamp<int64_t>(minimum, 0, INT32_MAX), high = std::clamp<int64_t>(maximum, low, INT32_MAX);
                amount = low + limitedRandom(random, uint32_t(high - low));
                bool deadly = damage.criticalChance > 0 && int(limitedRandom(random, 100)) < std::min(damage.criticalChance, 100);
                if (!deadly && damage.deadlyChance > 0) deadly = int(limitedRandom(random, 100)) < std::min(damage.deadlyChance, 100);
                if (deadly) amount *= 2;
            } else {
                amount = damage.minimum + limitedRandom(random, uint32_t(damage.maximum - damage.minimum + 1));
                if (sourceMonster && int(limitedRandom(random, 100)) < sourceMonster->rule.criticalChance) amount *= 2;
            }
            if (targetPlayer) amount = int64_t(mitigatePlayerDamage(float(amount) / 256.f, DamageType::Physical, targetPlayer->totals.character).dealt * 256.f);
            else amount = int64_t(mitigateMonsterDamage(float(amount) / 256.f, targetMonster->rule.resistances[0]) * 256.f);
        }
        DomainResult<> committed{DomainStatus::Applied, std::monostate{}};
        if (targetMonster) committed = ports_.monsters.damage(damage.target, damage.source, amount, tick.tick);
        else {
            ActorContext actor{targetPlayer->player, targetPlayer->actor, targetPlayer->area, area->generation, 0, tick.tick};
            committed = ports_.transactions.damage(actor, targetPlayer->characterRevision, amount);
        }
        if (committed) { ports_.random = random; it = state_.pending.erase(it); }
        else if (committed.status == DomainStatus::Capacity) { blocked = true; ++it; }
        else it = state_.pending.erase(it);
    }
    if (resolveSpells(tick) == StepStatus::Blocked) blocked = true;
    return blocked ? StepStatus::Blocked : StepStatus::Complete;
}
}
