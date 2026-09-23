#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include <algorithm>
#include <tuple>

namespace d2x {
namespace {
int chooseAttackMode(Enemy &enemy, const MonsterAiProfile &rules) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom) % 100 < unsigned(rules.params[3]) ? 1 : 2;
}
} // namespace
void Simulation::beginMonsterAttack(Enemy &enemy) {
    enemy.attackMode = 1;
    const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
    if (ai && (ai->kind == MonsterAiKind::Brute || ai->kind == MonsterAiKind::Skeleton ||
               ai->kind == MonsterAiKind::Zombie) &&
        monsterAttackTiming_ &&
        monsterAttackTiming_(enemy, 2) && monsterNormalCombat_ &&
        monsterAccuracy_ && monsterAccuracy_(enemy, 2))
        if (auto combat = monsterNormalCombat_(enemy.identity);
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
    if (enemy.attackImpact <= 0) {
        enemy.attackImpact = -1;
        resolveMonsterAttack(enemy);
    }
}
void Simulation::resolveMonsterAttack(Enemy &enemy) {
    auto &player = state_.player;
    if (enemy.hp <= 0 || player.dead || player.hp <= 0 || player.leapTime > 0 ||
        (player.pos - enemy.pos).length() >= monsterDefinition(enemy.kind).attackRange ||
        !grid_->segment(enemy.pos, player.pos))
        return;
    if (!(player.running && player.moving) && monsterAccuracy_)
        if (auto accuracy = monsterAccuracy_(enemy, enemy.attackMode)) {
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
    if (monsterNormalCombat_)
        if (auto combat = monsterNormalCombat_(enemy.identity)) {
            int minimum = combat->minDamage, maximum = combat->maxDamage;
            if (enemy.attackMode == 2 && combat->attack2Damage)
                std::tie(minimum, maximum) = *combat->attack2Damage;
            enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                 (enemy.combatRandom >> 32);
            const auto range = unsigned(maximum - minimum + 1);
            damage = float(minimum + uint32_t(enemy.combatRandom) % range);
        }
    player.hp = std::max(0.f, player.hp - damage);
    player.hitTime = .16f;
    if (wearEquipment_) wearEquipment_({}, true);
}
} // namespace d2x
