#include "gameplay/skills/behavior.hpp"
#include "gameplay/units/actions.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/combat/damage_request.hpp"
#include "gameplay/skills/runtime.hpp"
#include "gameplay/skills/necro_summon_spec.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/items/state.hpp"
#include "core/random.hpp"
#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <numbers>

namespace d2x {
namespace {
int chooseAttackMode(Enemy &enemy, const MonsterAiProfile &rules) {
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[3]) ? 1 : 2;
}
} // namespace
void Simulation::refreshMonsterAttackRate(Enemy &enemy) {
    if (enemy.attack <= 0 || enemy.attackMode >= 3 || enemy.teleportTarget) return;
    const auto combat = combatUnit(enemy.id).stats.attributes.combat;
    const int itemSpeed = enemy.necroPet && enemy.necroPet->item && combat.fasterAttack > 0 ? 120*combat.fasterAttack/(120+combat.fasterAttack) : 0;
    const int auraRate = combat.attackRate + itemSpeed;
    const int coldRate = enemy.chill > 0 && unitColdEffect_ ? unitColdEffect_(combatUnit(enemy.id)) : 0;
    const int rate = std::clamp(100 + auraRate + coldRate, 15, 175);
    if (rate == enemy.attackRatePercent) return;
    const float scale = float(enemy.attackRatePercent) / float(rate);
    rescaleTimedAction({enemy.attack, enemy.attackDuration, enemy.attackImpact}, scale);
    enemy.attackRatePercent = rate;
}
void Simulation::beginMonsterAttack(Enemy &enemy, int forcedMode) {
    monsterStopApproach(enemy);
    enemy.attackMode = forcedMode >= 3 ? forcedMode : forcedMode == 2 ? 2 : 1;
    const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
    if (!forcedMode && ai && (ai->kind == MonsterAiKind::Brute || ai->kind == MonsterAiKind::Skeleton ||
               ai->kind == MonsterAiKind::Zombie || ai->kind == MonsterAiKind::Fallen) &&
        monsterAttackTiming_ &&
        monsterAttackTiming_(enemy, 2) && monsterNormalCombat_ &&
        monsterAccuracy_ && monsterAccuracy_(enemy, state_.area.region, 2))
        if (auto combat = monsterNormalCombat_(enemy.identity, state_.area.region, enemy.enchantmentData());
            combat && combat->attack2Damage)
            enemy.attackMode = chooseAttackMode(enemy, *ai);
    const auto combat = combatUnit(enemy.id).stats.attributes.combat;
    const int itemSpeed = enemy.necroPet && enemy.necroPet->item && combat.fasterAttack > 0 ? 120*combat.fasterAttack/(120+combat.fasterAttack) : 0;
    const int auraRate = combat.attackRate + itemSpeed;
    const int coldRate = enemy.chill > 0 && unitColdEffect_ ? unitColdEffect_(combatUnit(enemy.id)) : 0;
    enemy.attackRatePercent = enemy.attackMode >= 3 ? 100 : std::clamp(100 + auraRate + coldRate, 15, 175);
    const float chillScale = 100.f / float(enemy.attackRatePercent);
    if (auto timing = monsterAttackTiming_ ? monsterAttackTiming_(enemy, enemy.attackMode) : std::nullopt) {
        enemy.attackDuration = timing->duration * chillScale;
        enemy.attackImpact = timing->impact * chillScale;
    } else {
        enemy.attackDuration = monsterDefinition(enemy.kind).attackInterval * chillScale;
        enemy.attackImpact = 0;
    }
    enemy.attack = enemy.attackDuration;
    enemy.attackEventIndex = 0;
    if (enemy.kind == MonsterKind::Andariel && enemy.attackMode == 3) {
        enemy.skillPosition = monsterTargetPosition(enemy);
        if (auto timing = monsterAttackTiming_(enemy, 3); timing && !timing->eventTimes.empty())
            enemy.attackImpact = timing->eventTimes.front();
    }
    updateMonsterSpectralDamage(enemy);
    emit(EnemyAttacked{enemy.id, enemy.kind, enemy.attackMode});
    if (enemy.attackImpact <= 0) {
        enemy.attackImpact = -1;
        if (enemy.identity.superUnique == "The Countess" && enemy.attackMode == 3) {
            launchCountessFirewall(enemy);
            return;
        }
        if (enemy.kind == MonsterKind::BloodRaven && enemy.attackMode == 4) {
            launchMonsterProjectile(enemy);
            return;
        }
        if (enemy.attackMode == 3 && monsterResurrection_ &&
            monsterResurrection_(enemy))
            resolveMonsterResurrection(enemy);
        else if (enemy.attackMode == 3 && monsterWeb_ && monsterWeb_(enemy))
            activateSpiderWeb(enemy);
        else if (enemy.attackMode >= 3)
            launchMonsterSpell(enemy);
        else if (monsterProjectile_ && monsterProjectile_(enemy, enemy.attackMode))
            launchMonsterProjectile(enemy);
        else
            resolveMonsterAttack(enemy);
    }
}
void Simulation::launchMonsterProjectile(Enemy &enemy) {
    const auto projectile = monsterProjectile_ ? monsterProjectile_(enemy, enemy.attackMode) : std::nullopt;
    if (!projectile || projectile->id < 0 || projectile->velocity <= 0 || projectile->lifetime <= 0)
        throw std::runtime_error("Monster projectile is missing");
    const auto direction = (monsterTargetPosition(enemy) - enemy.pos).unit();
    auto &missile = skills().launchStraight({{}, enemy.id, enemy.pos,
        direction * projectile->velocity, projectile->lifetime, SkillBehavior::None,
        true, projectile->id, 0, 0, 0, true, enemy.attackMode});
    replicateMonsterMissile(enemy, missile);
}
void Simulation::launchMonsterSpell(Enemy &enemy) {
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, enemy.attackMode) : std::nullopt;
    if (!spell || spell->projectile.id < 0 || spell->projectile.velocity <= 0 ||
        spell->projectile.lifetime <= 0 || spell->maximumDamage < spell->minimumDamage)
        throw std::runtime_error("Monster spell projectile is missing");
    auto direction = (monsterTargetPosition(enemy) - enemy.pos).unit();
    const float damage = float(spell->minimumDamage +
        monsterAiRandom(enemy) % unsigned(spell->maximumDamage - spell->minimumDamage + 1));
    if (enemy.kind == MonsterKind::Andariel && enemy.attackMode == 3 && enemy.skillPosition) {
        constexpr int horizontal[]{0,-1,-1,-1,0,1,1,1,0,-1,-2,-2,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-3,-3,-3,0,3,3,3};
        constexpr int vertical[]{-1,-1,0,1,1,1,0,-1,-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-1,-2,-2,-3,-3,0,3,3,3,0,-3};
        constexpr int origins[]{29,28,27,26,25,24,31,30};
        constexpr int fan[8][9]{{27,14,15,3,99,7,21,22,31},{26,12,13,2,99,6,19,20,30},
            {25,10,11,1,99,5,17,18,29},{24,8,9,0,99,4,15,16,28},
            {31,22,23,7,99,3,13,14,27},{30,20,7,6,99,2,1,12,26},
            {29,18,19,5,99,1,9,10,25},{28,16,17,4,99,0,23,8,24}};
        const Vec aim = *enemy.skillPosition - enemy.pos;
        const int facing = (int(std::floor(std::atan2(aim.y, aim.x) *
            4.f / std::numbers::pi_v<float> + .5f)) + 7) & 7;
        const int origin = origins[facing];
        Vec target{float(int(enemy.pos.x) + horizontal[origin]), float(int(enemy.pos.y) + vertical[origin])};
        const int offset = fan[facing][std::min<size_t>(enemy.attackEventIndex, 8)];
        if (offset != 99) target = target + Vec{float(horizontal[offset]), float(vertical[offset])};
        direction = (target - Vec{float(int(enemy.pos.x)), float(int(enemy.pos.y))}).unit();
    }
    auto &missile = skills().launchStraight({{}, enemy.id, enemy.pos,
        direction * spell->projectile.velocity, spell->projectile.lifetime, SkillBehavior::None,
        false, spell->projectile.id, damage, 0, 0, true, enemy.attackMode});
    missile.killOnHit = spell->killOnHit;
    replicateMonsterMissile(enemy, missile);
}
bool Simulation::monsterMeleeReach(const Enemy &enemy, EntityId defender, int rangeBonus) {
    if (!defender) defender = enemy.combatTarget;
    const auto target = combatUnit(defender);
    if (!target.alive()) return false;
    const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
    const int size = enemy.intrinsicCombat ? enemy.intrinsicCombat->collisionSize
                                         : monsterSize_ ? monsterSize_(enemy) : 2;
    const bool inReach = ai || enemy.intrinsicCombat
        ? meleeDistance(enemy.pos, size, *target.position, target.stats.collisionSize) <= (ai ? ai->meleeRange : 0) + rangeBonus + 1
        : (*target.position - enemy.pos).length() < monsterDefinition(enemy.kind).attackRange;
    return inReach && grid_->segment(enemy.pos, *target.position);
}
void Simulation::resolveMonsterAttack(Enemy &enemy, int modeOverride, bool projectile, EntityId defender) {
    if (!defender) defender = enemy.combatTarget;
    auto target = combatUnit(defender);
    if (!target.alive() || !canAttack(enemy.id, defender) || (!projectile && enemy.hp <= 0)) return;
    if (!projectile && !monsterMeleeReach(enemy, defender, enemy.amazonPet ? enemy.amazonPet->weapon.rangeAdder : enemy.intrinsicCombat ? 0 : 3)) return;
    const int mode = modeOverride ? modeOverride : enemy.attackMode;
    const auto source = combatUnit(enemy.id);
    const auto accuracy = enemy.intrinsicCombat ? std::optional<MonsterAccuracy>{{source.stats.level, source.stats.attributes.attackRating}} :
        monsterAccuracy_ ? monsterAccuracy_(enemy, state_.area.region, mode) : std::nullopt;
    const EntityId item = enemy.amazonPet ? enemy.amazonPet->weapon.item :
        enemy.necroPet && enemy.necroPet->item ? enemy.necroPet->item->id : EntityId{};
    const bool ironItem = bool(item);
    auto targetModifiers = source.stats.attributes.combat.target;
    if (ironItem) {
        const auto weapon = source.stats.attributes.combat.weapons.find(item);
        if (weapon != source.stats.attributes.combat.weapons.end()) mergeAttackTargetModifiers(targetModifiers,weapon->second.target);
    }
    const MonsterDefense defense{target.stats.level,target.stats.attributes.defense,target.stats.demon,target.stats.undead,target.stats.boss};
    const bool running = target.player && target.records.player->movement.runningNow && target.records.player->movement.moving;
    if (!running && accuracy) {
        const int auraRating = enemy.combatEffects.modifiers(state_.frame).combat.attackRatingPercent +
            (enemy.enchantment ? enemy.enchantment->attackRatingPercent : 0);
        const int chance = ironItem ? weaponHitChance(accuracy->level,accuracy->attackRating,auraRating,targetModifiers,defense,target.stats.rank) :
            physicalHitChance(accuracy->level,int(int64_t(accuracy->attackRating) * std::max(0, 100 + auraRating) / 100),
                target.stats.level, target.stats.attributes.defense);
        if (limitedRandom(enemy.combatRandom, 100) >= unsigned(chance)) {
            if (!projectile) skills().triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, enemy.id);
            return;
        }
    }
    if (!ironItem && target.stats.block > 0 && limitedRandom(*target.random, 100) < unsigned(target.stats.block)) {
        blockUnit(defender);
        if (!projectile) skills().triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, enemy.id);
        return;
    }
    if (!ironItem) if (const int avoided = avoidCombatHit(target, projectile, [this] {
        const auto *weapon = attackWeapon(false, false);
        return weapon && attackTiming_ ? attackTiming_(*weapon, false, false, "s1") : std::nullopt;
    }); avoided >= 0) {
        if (target.player) emit(SkillCast{target.id, avoided, *target.position});
        return;
    }
    float damage = monsterDefinition(enemy.kind).damage;
    const auto combat = monsterNormalCombat_
                            ? monsterNormalCombat_(enemy.identity, state_.area.region, enemy.enchantmentData()) : std::nullopt;
    if (enemy.intrinsicCombat) {
        const int minimum = int(source.stats.minimumDamage * 256.f), maximum = int(source.stats.maximumDamage * 256.f);
        damage = float(minimum + int(limitedRandom(enemy.combatRandom, unsigned(std::max(0, maximum - minimum))))) / 256.f;
    } else if (combat) {
        auto range = mode == 2 ? combat->attack2Damage : combat->attack1Damage;
        if (!range && (projectile || mode == 2)) damage = 0;
        if (range) {
            const auto [minimum, maximum] = *range;
            rollRandom(enemy.combatRandom);
            damage = float(minimum + uint32_t(enemy.combatRandom) % unsigned(maximum - minimum + 1));
        }
    }
    if (projectile && monsterProjectile_)
        if (auto spec = monsterProjectile_(enemy, mode)) {
            damage = damage * float(spec->sourceDamage) / 128.f;
            rollRandom(enemy.combatRandom);
            damage += float(spec->minimumDamage +
                            uint32_t(enemy.combatRandom) %
                                unsigned(spec->maximumDamage - spec->minimumDamage + 1));
        }
    if (enemy.intrinsicCombat && !enemy.amazonPet && limitedRandom(enemy.combatRandom, 100) < unsigned(source.stats.critical)) damage *= 2.f;
    if (!enemy.intrinsicCombat && monsterCriticalChance_)
        if (auto chance = monsterCriticalChance_(enemy, state_.area.region); chance && *chance > 0) {
            rollRandom(enemy.combatRandom);
            if (uint32_t(enemy.combatRandom) % 100 < unsigned(*chance)) damage *= 2.f;
        }
    const int damagePercent = (enemy.enchantment ? enemy.enchantment->damagePercent : 0) +
        source.stats.attributes.combat.damagePercent + (ironItem ? targetDamageBonus(targetModifiers,defense) : 0);
    // SUnitDmg adds the signed percentage to the base and floors ED at -90.
    // Preserve its truncation toward zero for reductions such as Weaken.
    const int64_t physicalBase = int64_t(damage * 256.f);
    damage = float(std::max<int64_t>(0, physicalBase + physicalBase * std::max(-90, damagePercent) / 100)) / 256.f;
    if (ironItem) {
        const auto &modifiers = source.stats.attributes.combat;
        auto elements = rollAttackElements(item, &modifiers, nullptr, &enemy.combatRandom);
        if (enemy.amazonPet && !elements.deadly && limitedRandom(enemy.combatRandom, 100) < unsigned(source.stats.critical)) elements.deadly = true;
        elements.attackerLevel = source.stats.level;
        resolveWeaponHit(defender, damage, enemy.id, elements);
        return;
    }
    const float previousLife = *target.life;
    if (!projectile) skills().triggerCombatEffects(defender, CombatEffectEvent::AttackedInMelee, enemy.id);
    DamageRequest hit{enemy.id, defender, damage, MonsterDamageType::Physical, 0, false, false};
    const auto projectileSpec = projectile && monsterProjectile_ ? monsterProjectile_(enemy, mode) : std::nullopt;
    const int sourceDamage = projectileSpec ? projectileSpec->sourceDamage : 128;
    const auto &elements = source.stats.attributes.combat;
    if (!enemy.enchantment) for (auto [minimum, maximum, type] : {
        std::tuple{elements.fireMinimum, elements.fireMaximum, MonsterDamageType::Fire},
        std::tuple{elements.lightningMinimum, elements.lightningMaximum, MonsterDamageType::Lightning},
        std::tuple{elements.coldMinimum, elements.coldMaximum, MonsterDamageType::Cold}})
        if (maximum > 0) {
            const float amount = float(minimum * 256 + limitedRandom(enemy.combatRandom,
                unsigned(std::max(0, maximum - minimum) * 256))) / 256.f;
            hit.channels[size_t(type)] = float(int64_t(amount * 256.f) * sourceDamage / 128) / 256.f;
        }
    if (combat && (!enemy.intrinsicCombat || (enemy.necroPet && enemy.necroPet->spec->kind == NecroSummonKind::Revive))) prepareMonsterElements(enemy, *combat, mode, hit, sourceDamage);
    prepareMonsterEnchantmentHit(enemy, hit, sourceDamage);
    const auto physicalDamage = resolveIncoming(enemy.id, target, hit.amount, MonsterDamageType::Physical);
    hit.amount = physicalDamage.dealt;
    restoreUnit(defender, physicalDamage.absorbed);
    for (size_t channel = 1; channel < hit.channels.size(); ++channel) {
        if (hit.channels[channel] <= 0) continue;
        const auto resolved = resolveIncoming(enemy.id, target, hit.channels[channel], MonsterDamageType(channel));
        hit.channels[channel] = resolved.dealt;
        restoreUnit(defender, resolved.absorbed);
    }
    hit.mitigated = true;
    skills().healLifeTap(enemy.id, defender, hit.amount, projectile);
    if (!projectile && enemy.necroPet && enemy.necroPet->spec->lifeLeechPercent > 0 &&
        (target.player || (target.monster && target.identity.role == CombatRole::Monster))) {
        const auto &blood = *enemy.necroPet->spec;
        const int drain = target.player ? 100 : target.stats.drain;
        const int64_t damage = std::min<int64_t>(int64_t(hit.amount*256.f)*drain/100, int64_t(previousLife*256.f));
        float healing = float(damage*blood.lifeLeechPercent/100)/256.f;
        const auto owner = combatUnit(enemy.allegiance.owner);
        auto heal = [&](EntityId id, float amount) {
            const auto unit = combatUnit(id); if (!unit.alive()) return 0.f;
            const float before = *unit.life; restoreUnit(id, amount); return *unit.life-before;
        };
        if (owner.alive()) healing -= heal(owner.id, float(int64_t(healing*256.f)*blood.ownerSharePercent/100)/256.f);
        healing -= heal(enemy.id, healing);
        if (owner.alive() && healing > 0) heal(owner.id, healing);
        if (damage > 0 && blood.healOverlay >= 0) {
            state_.area.effects.push_back({enemy.pos,0,blood.healOverlayDuration,-1,blood.healOverlay,enemy.id});
            if (owner.alive()) state_.area.effects.push_back({*owner.position,0,blood.healOverlayDuration,-1,blood.healOverlay,owner.id});
        }
    }
    applyMonsterCurse(enemy, defender);
    if (hit.chill > 0) {
        applyChill(defender, hit.chill);
        hit.chill = 0;
    }
    if (!projectile) skills().reflectThorns(enemy.id, defender, hit.amount);
    if (!projectile) skills().reflectIronMaiden(enemy.id, defender, hit.amount);
    const float hitDealt = dealDamage(hit);
    if (!projectile && hitDealt > 0 && enemy.hp > 0)
        skills().triggerCombatEffects(defender, CombatEffectEvent::DamagedInMelee, enemy.id);
    if (!projectile && hitDealt > 0 && enemy.hp > 0)
        skills().triggerCombatEffects(enemy.id, CombatEffectEvent::DealtMeleeDamage, defender);
    const float total = previousLife - *target.life;
    recoverUnit(defender, enemy.id, total, total > hitDealt ||
        std::any_of(hit.channels.begin() + 1, hit.channels.end(), [](float amount) { return amount > 0; }));
    if (target.player && wearEquipment_) wearEquipment_({}, true);
}
} // namespace d2x
