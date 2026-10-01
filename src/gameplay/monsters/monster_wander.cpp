#include "core/random.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace d2x {
uint32_t monsterAiRandom(Enemy &enemy) {
    rollRandom(enemy.combatRandom);
    return uint32_t(enemy.combatRandom);
}

void monsterStartApproach(Enemy &enemy, int stopDistance, int velocityPercent, bool running,
                          std::optional<Vec> destination) {
    enemy.approach = MonsterApproach{destination, stopDistance, velocityPercent, running};
    enemy.aiPursuing = true;
    enemy.aiRunning = running;
    enemy.route.clear();
    enemy.rethink = 0;
}
void monsterStopApproach(Enemy &enemy) {
    if (!enemy.approach) return;
    enemy.approach.reset();
    enemy.aiPursuing = enemy.aiRunning = false;
    enemy.movementVelocityPercent.reset();
    enemy.route.clear();
    enemy.rethink = 0;
}
Vec monsterRadiusApproachTarget(Vec from, int size, Vec target, int radius) {
    // AiTactics::MoveInRadiusToTarget(..., radius, 0), with the original
    // integer coordinate offsets and AIUTIL's full-unit-size distance.
    const int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    const int dx = int(std::floor(target.x)) - x, dy = int(std::floor(target.y)) - y;
    const int ax = std::abs(dx), ay = std::abs(dy);
    const int sx = std::abs(ax - size), sy = std::abs(ay - size);
    const int distance = std::max(sx, sy) + std::min(sx, sy) / 2;
    const int advance = std::min(distance, radius);
    const int sum = std::max(ax + ay, advance);
    int offsetX = sum > 0 ? advance * ax / sum : 0;
    int offsetY = sum > 0 ? advance * ay / sum : 0;
    while (offsetX + offsetY < advance) { ++offsetX; ++offsetY; }
    return {float(x + offsetX * ((dx > 0) - (dx < 0))) + .5f,
            float(y + offsetY * ((dy > 0) - (dy < 0))) + .5f};
}

std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius, MovementCollisionRule rule) {
    if (radius <= 0) return std::nullopt;
    for (int attempt = 0; attempt < 4; ++attempt) {
        int x = radius, y = int(monsterAiRandom(enemy) % unsigned(radius));
        if (monsterAiRandom(enemy) & 1) std::swap(x, y);
        if (monsterAiRandom(enemy) & 1) x = -x;
        if (monsterAiRandom(enemy) & 1) y = -y;
        const Vec target = enemy.pos + Vec{float(x), float(y)};
        if (grid.walkable(target, rule) && grid.segment(enemy.pos, target, {}, rule)) return target;
    }
    return std::nullopt;
}
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule) {
    const Vec offset = enemy.pos - target;
    if (offset.length() == 0) return false;
    const Vec away = offset.unit();
    const Vec tangent{-away.y, away.x};
    for (float side : {0.f, .5f, -.5f, 1.f, -1.f}) {
        const Vec destination = enemy.pos + (away + tangent * side).unit() *
                                float(std::max(distance, 1));
        if (!grid.walkable(destination, rule)) continue;
        auto route = grid.path(enemy.pos, destination, false, rule);
        if (route.empty()) continue;
        monsterStopApproach(enemy);
        enemy.route = std::move(route);
        enemy.aiEscaping = true;
        enemy.rethink = 0;
        return true;
    }
    return false;
}
bool monsterStartCircle(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule) {
    const Vec offset = enemy.pos - target;
    if (offset.length() == 0) return false;
    const Vec radial = offset.unit();
    const bool clockwise = (monsterAiRandom(enemy) & 255) < 128;
    const Vec tangent = clockwise ? Vec{-radial.y, radial.x} : Vec{radial.y, -radial.x};
    for (float angleStep : {1.f, .75f, .5f}) {
        const Vec destination = target + (radial + tangent * angleStep).unit() *
                                        float(std::max(distance, 1));
        if (!grid.walkable(destination, rule)) continue;
        auto route = grid.path(enemy.pos, destination, false, rule);
        if (route.empty()) continue;
        monsterStopApproach(enemy);
        enemy.route = std::move(route);
        enemy.aiCircling = true;
        enemy.aiWait = 0;
        return true;
    }
    return false;
}
void monsterAdvanceCircle(Enemy &enemy, const Grid &grid, float speed, float dt, MovementCollisionRule rule) {
    while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
        enemy.route.pop_front();
    if (enemy.route.empty()) {
        enemy.aiCircling = false;
        return;
    }
    const Vec offset = enemy.route.front() - enemy.pos;
    const Vec next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
    if (!grid.segment(enemy.pos, next, {}, rule)) {
        enemy.route.clear();
        enemy.aiCircling = false;
        return;
    }
    enemy.pos = next;
    if ((enemy.route.front() - enemy.pos).length() < .25f) enemy.route.pop_front();
    if (enemy.route.empty()) enemy.aiCircling = false;
}
} // namespace d2x
