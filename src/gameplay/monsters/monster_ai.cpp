#include "gameplay/simulation/simulation.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/monsters/skeleton_ai.hpp"
#include "gameplay/monsters/brute_ai.hpp"
#include <algorithm>

namespace d2x {
void Simulation::updateMonsters(float dt) {
    auto &player = state_.player;
    for (auto &enemy : state_.area.enemies) {
        if (enemy.hp <= 0 || !active(enemy.pos))
            continue;
        enemy.chill = std::max(0.f, enemy.chill - dt);
        enemy.stun = std::max(0.f, enemy.stun - dt);
        enemy.attack = std::max(0.f, enemy.attack - dt);
        enemy.rethink = std::max(0.f, enemy.rethink - dt);
        enemy.aiWait = std::max(0.f, enemy.aiWait - dt);
        if (player.dead || player.hp <= 0) {
            enemy.route.clear();
            enemy.aiPursuing = false;
            continue;
        }
        if (enemy.stun > 0)
            continue;
        const auto &definition = monsterDefinition(enemy.kind);
        auto delta = player.pos - enemy.pos;
        float distance = delta.length();
        if (distance >= definition.sightRange) {
            enemy.route.clear();
            enemy.rethink = 0;
            enemy.aiPursuing = false;
            continue;
        }
        const auto ai = monsterAi_ ? monsterAi_(enemy) : std::nullopt;
        const bool skeletonAi = ai && ai->kind == MonsterAiKind::Skeleton;
        const bool bruteAi = ai && ai->kind == MonsterAiKind::Brute;
        bool clear = grid_->segment(enemy.pos, player.pos);
        if (distance >= definition.attackRange || !clear) {
            if (skeletonAi && !skeletonApproaches(enemy, *ai)) {
                enemy.route.clear();
                continue;
            }
            Vec destination = player.pos;
            if (clear) {
                enemy.route.clear();
                enemy.rethink = 0;
            } else {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front())) {
                    enemy.route.clear();
                    enemy.rethink = 0;
                }
                if (enemy.rethink <= 0) {
                    enemy.route = grid_->path(enemy.pos, player.pos);
                    enemy.rethink = .7f;
                }
                if (enemy.route.empty())
                    continue;
                destination = enemy.route.front();
            }
            auto offset = destination - enemy.pos;
            auto originalSpeed = monsterWalkSpeed_ ? monsterWalkSpeed_(enemy) : std::nullopt;
            float speed = originalSpeed.value_or(definition.speed) * (enemy.chill > 0 ? .42f : 1.f);
            if (bruteAi) speed *= bruteWalkMultiplier(enemy);
            auto next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
            if (grid_->segment(enemy.pos, next))
                enemy.pos = next;
            else {
                enemy.route.clear();
                enemy.rethink = 0;
            }
        } else {
            enemy.route.clear();
            enemy.rethink = 0;
        }
        if ((player.pos - enemy.pos).length() < definition.attackRange && enemy.attack <= 0 &&
            player.leapTime <= 0 && grid_->segment(enemy.pos, player.pos)) {
            if (skeletonAi && !skeletonAttacks(enemy, *ai))
                continue;
            enemy.attack = definition.attackInterval * (enemy.chill > 0 ? 2.f : 1.f);
            if (!(player.running && player.moving) && monsterAccuracy_) {
                if (auto accuracy = monsterAccuracy_(enemy)) {
                    const auto chance = physicalHitChance(accuracy->level, accuracy->attackRating,
                                                           equipmentStats_.level, equipmentStats_.defense);
                    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                         (enemy.combatRandom >> 32);
                    if (uint32_t(enemy.combatRandom) % 100 >= chance)
                        continue;
                }
            }
            int block = equipmentStats_.blockChance;
            if (player.running && player.moving)
                block /= 3;
            if (block > 0) {
                player.combatRandom = uint64_t(uint32_t(player.combatRandom)) * 0x6ac690c5ULL +
                                      (player.combatRandom >> 32);
                if (uint32_t(player.combatRandom) % 100 < unsigned(block))
                    continue;
            }
            float damage = definition.damage;
            if (monsterNormalCombat_)
                if (auto combat = monsterNormalCombat_(enemy.identity)) {
                    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                                         (enemy.combatRandom >> 32);
                    const auto range = unsigned(combat->maxDamage - combat->minDamage + 1);
                    damage = float(combat->minDamage + uint32_t(enemy.combatRandom) % range);
                }
            player.hp = std::max(0.f, player.hp - damage);
            player.hitTime = .16f;
            if (wearEquipment_)
                wearEquipment_({}, true);
        }
    }
}
} // namespace d2x
