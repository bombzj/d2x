#include "gameplay/simulation/simulation.hpp"
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
        if (player.dead || player.hp <= 0) {
            enemy.route.clear();
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
            continue;
        }
        bool clear = grid_->segment(enemy.pos, player.pos);
        if (distance >= definition.attackRange || !clear) {
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
            float speed = definition.speed * (enemy.chill > 0 ? .42f : 1.f);
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
            enemy.attack = definition.attackInterval * (enemy.chill > 0 ? 2.f : 1.f);
            if (!(player.running && player.moving) && monsterAccuracy_) {
                if (auto accuracy = monsterAccuracy_(enemy)) {
                    const int64_t divisor = int64_t(accuracy->attackRating) + equipmentStats_.defense;
                    const int64_t factor = divisor ? int64_t(100) * accuracy->attackRating / divisor : 100;
                    const auto chance = std::clamp(int64_t(2) * accuracy->level * factor /
                                                       (int64_t(accuracy->level) + equipmentStats_.level),
                                                   int64_t(5), int64_t(95));
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
            player.hp = std::max(0.f, player.hp - definition.damage);
            player.hitTime = .16f;
            if (wearEquipment_)
                wearEquipment_({}, true);
        }
    }
}
} // namespace d2x
