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
            player.hp = std::max(0.f, player.hp - definition.damage);
            player.hitTime = .16f;
            enemy.attack = definition.attackInterval * (enemy.chill > 0 ? 2.f : 1.f);
        }
    }
}
} // namespace d2x
