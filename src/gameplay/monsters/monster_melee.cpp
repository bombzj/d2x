#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/monsters/monster_wander.hpp"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace d2x {
namespace {
int chooseAttackMode(Enemy &enemy, const MonsterAiProfile &rules) {
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[3]) ? 1 : 2;
}
} // namespace
void Simulation::beginMonsterAttack(Enemy &enemy, int forcedMode) {
    enemy.attackMode = forcedMode >= 3 ? forcedMode : forcedMode == 2 ? 2 : 1;
    const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
    if (!forcedMode && ai && (ai->kind == MonsterAiKind::Brute || ai->kind == MonsterAiKind::Skeleton ||
               ai->kind == MonsterAiKind::Zombie || ai->kind == MonsterAiKind::Fallen) &&
        monsterAttackTiming_ &&
        monsterAttackTiming_(enemy, 2) && monsterNormalCombat_ &&
        monsterAccuracy_ && monsterAccuracy_(enemy, state_.area.region, 2))
        if (auto combat = monsterNormalCombat_(enemy.identity, state_.area.region);
            combat && combat->attack2Damage)
            enemy.attackMode = chooseAttackMode(enemy, *ai);
    const float chillScale = enemy.chill > 0 ? 2.f : 1.f;
    if (auto timing = monsterAttackTiming_ ? monsterAttackTiming_(enemy, enemy.attackMode) : std::nullopt) {
        enemy.attackDuration = timing->duration * chillScale;
        enemy.attackImpact = timing->impact * chillScale;
    } else {
        enemy.attackDuration = monsterDefinition(enemy.kind).attackInterval * chillScale;
        enemy.attackImpact = 0;
    }
    enemy.attack = enemy.attackDuration;
    emit(EnemyAttacked{enemy.id, enemy.kind, enemy.attackMode});
    if (enemy.attackImpact <= 0) {
        enemy.attackImpact = -1;
        if (enemy.attackMode >= 3)
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
    const auto direction = (state_.player.pos - enemy.pos).unit();
    state_.area.missiles.push_back({ids_.allocate(), enemy.id, enemy.pos,
        direction * projectile->velocity, projectile->lifetime, Skill::Fireball,
        true, projectile->id, 0, 0, 0, true, enemy.attackMode});
}
void Simulation::launchMonsterSpell(Enemy &enemy) {
    const auto spell = monsterSpell_ ? monsterSpell_(enemy, enemy.attackMode) : std::nullopt;
    if (!spell || spell->projectile.id < 0 || spell->projectile.velocity <= 0 ||
        spell->projectile.lifetime <= 0 || spell->maximumDamage < spell->minimumDamage)
        throw std::runtime_error("Monster spell projectile is missing");
    const auto direction = (state_.player.pos - enemy.pos).unit();
    const float damage = float(spell->minimumDamage +
        monsterAiRandom(enemy) % unsigned(spell->maximumDamage - spell->minimumDamage + 1));
    state_.area.missiles.push_back({ids_.allocate(), enemy.id, enemy.pos,
        direction * spell->projectile.velocity, spell->projectile.lifetime, Skill::Fireball,
        false, spell->projectile.id, damage, 0, 0, true, enemy.attackMode});
}
void Simulation::resolveMonsterAttack(Enemy &enemy, int modeOverride, bool projectile) {
    auto &player = state_.player;
    if ((!projectile && enemy.hp <= 0) || player.dead || player.hp <= 0 || player.leapTime > 0 ||
        (!projectile && ((player.pos - enemy.pos).length() >= monsterDefinition(enemy.kind).attackRange ||
                         !grid_->segment(enemy.pos, player.pos))))
        return;
    const int mode = modeOverride ? modeOverride : enemy.attackMode;
    if (!(player.running && player.moving) && monsterAccuracy_)
        if (auto accuracy = monsterAccuracy_(enemy, state_.area.region, mode)) {
            const auto chance = physicalHitChance(accuracy->level, accuracy->attackRating,
                                                   equipmentStats_.level, equipmentStats_.defense);
            enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                 (enemy.combatRandom >> 32);
            if (uint32_t(enemy.combatRandom) % 100 >= unsigned(chance)) return;
        }
    int block = equipmentStats_.blockChance;
    if (player.running && player.moving) block /= 3;
    if (block > 0) {
        player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                              (player.combatRandom >> 32);
        if (uint32_t(player.combatRandom) % 100 < unsigned(block)) return;
    }
    float damage = monsterDefinition(enemy.kind).damage;
    const auto combat = monsterNormalCombat_
                            ? monsterNormalCombat_(enemy.identity, state_.area.region) : std::nullopt;
    if (combat) {
        auto range = mode == 2 ? combat->attack2Damage : combat->attack1Damage;
        if (!range && (projectile || mode == 2)) damage = 0;
        if (range) {
            const auto [minimum, maximum] = *range;
            enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                 (enemy.combatRandom >> 32);
            damage = float(minimum + uint32_t(enemy.combatRandom) % unsigned(maximum - minimum + 1));
        }
    }
    if (projectile && monsterProjectile_)
        if (auto spec = monsterProjectile_(enemy, mode)) {
            damage = damage * float(spec->sourceDamage) / 128.f;
            enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                 (enemy.combatRandom >> 32);
            damage += float(spec->minimumDamage +
                            uint32_t(enemy.combatRandom) %
                                unsigned(spec->maximumDamage - spec->minimumDamage + 1));
        }
    if (monsterCriticalChance_)
        if (auto chance = monsterCriticalChance_(enemy, state_.area.region); chance && *chance > 0) {
            enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                 (enemy.combatRandom >> 32);
            if (uint32_t(enemy.combatRandom) % 100 < unsigned(*chance)) damage *= 2.f;
        }
    player.hp = std::max(0.f, player.hp - damage);
    if (combat && player.hp > 0)
        applyMonsterElements(enemy, *combat, mode);
    player.hitTime = .16f;
    if (wearEquipment_) wearEquipment_({}, true);
}
} // namespace d2x
