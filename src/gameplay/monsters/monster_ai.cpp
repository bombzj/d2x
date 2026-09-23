#include "gameplay/simulation/simulation.hpp"
#include "gameplay/monsters/skeleton_ai.hpp"
#include "gameplay/monsters/brute_ai.hpp"
#include "gameplay/monsters/zombie_ai.hpp"
#include <algorithm>

namespace d2x {
void Simulation::updateMonsters(float dt) {
    auto &player = state_.player;
    for (auto &enemy : state_.area.enemies) {
        if (enemy.hp <= 0 || !active(enemy.pos))
            continue;
        enemy.chill = std::max(0.f, enemy.chill - dt);
        enemy.stun = std::max(0.f, enemy.stun - dt);
        enemy.rethink = std::max(0.f, enemy.rethink - dt);
        enemy.aiWait = std::max(0.f, enemy.aiWait - dt);
        if (player.dead || player.hp <= 0) {
            enemy.route.clear();
            enemy.aiPursuing = false;
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.attackMode = 1;
            continue;
        }
        if (enemy.stun > 0) {
            enemy.attack = enemy.attackDuration = 0;
            enemy.attackImpact = -1;
            enemy.attackMode = 1;
            continue;
        }
        if (enemy.attack > 0) {
            enemy.attack = std::max(0.f, enemy.attack - dt);
            if (enemy.attackImpact >= 0) {
                enemy.attackImpact -= dt;
                if (enemy.attackImpact <= 0) {
                    enemy.attackImpact = -1;
                    resolveMonsterAttack(enemy);
                }
            }
            if (enemy.attack == 0) {
                enemy.attackDuration = 0;
                enemy.attackImpact = -1;
                enemy.attackMode = 1;
            }
            continue;
        }
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
        const bool zombieAi = ai && ai->kind == MonsterAiKind::Zombie;
        bool clear = grid_->segment(enemy.pos, player.pos);
        if (distance >= definition.attackRange || !clear) {
            if (skeletonAi && !skeletonApproaches(enemy, *ai)) {
                enemy.route.clear();
                continue;
            }
            const bool zombieWanders = zombieAi && !zombiePursues(enemy, *ai, distance);
            Vec destination = player.pos;
            if (zombieWanders) {
                while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
                    enemy.route.pop_front();
                if (!enemy.route.empty() && !grid_->segment(enemy.pos, enemy.route.front()))
                    enemy.route.clear();
                if (enemy.route.empty())
                    if (auto target = zombieWanderTarget(enemy, *grid_)) enemy.route.push_back(*target);
                if (enemy.route.empty()) continue;
                destination = enemy.route.front();
                enemy.rethink = 0;
            } else if (clear) {
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
            if (zombieAi && !zombieWanders) speed *= 4.f / 3.f;
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
        if ((player.pos - enemy.pos).length() < definition.attackRange &&
            player.leapTime <= 0 && grid_->segment(enemy.pos, player.pos)) {
            if (skeletonAi && !skeletonAttacks(enemy, *ai))
                continue;
            beginMonsterAttack(enemy);
        }
    }
}
} // namespace d2x
