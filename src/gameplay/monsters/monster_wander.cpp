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
int monsterAiDistance(Vec from, int size, Vec target) {
    const int offsetX = std::abs(std::abs(int(std::floor(target.x)) - int(std::floor(from.x))) - size);
    const int offsetY = std::abs(std::abs(int(std::floor(target.y)) - int(std::floor(from.y))) - size);
    return std::max(offsetX, offsetY) + std::min(offsetX, offsetY) / 2;
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
Vec monsterRadiusApproachTarget(Vec from, int size, Vec target, int radius, int targetDistance) {
    const int x = int(std::floor(from.x)), y = int(std::floor(from.y));
    const int dx = int(std::floor(target.x)) - x, dy = int(std::floor(target.y)) - y;
    const int ax = std::abs(dx), ay = std::abs(dy);
    const int sx = std::abs(ax - size), sy = std::abs(ay - size);
    const int distance = std::max(sx, sy) + std::min(sx, sy) / 2;
    const int difference = distance - targetDistance;
    const int sign = (difference > 0) - (difference < 0);
    const int advance = std::min(std::abs(difference), radius);
    const int sum = std::max(ax + ay, advance);
    int offsetX = sum > 0 ? advance * ax / sum : 0;
    int offsetY = sum > 0 ? advance * ay / sum : 0;
    while (offsetX + offsetY < advance) { ++offsetX; ++offsetY; }
    return {float(x + sign * offsetX * ((dx > 0) - (dx < 0))) + .5f,
            float(y + sign * offsetY * ((dy > 0) - (dy < 0))) + .5f};
}

std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius, MovementCollisionRule rule) {
    if (radius <= 0) return std::nullopt;
    const bool horizontal = (monsterAiRandom(enemy) & 1) != 0;
    const int offset = int(monsterAiRandom(enemy) % unsigned(radius));
    int offsetX = horizontal ? radius : offset, offsetY = horizontal ? offset : radius;
    if (monsterAiRandom(enemy) & 1) offsetX = -offsetX;
    if (monsterAiRandom(enemy) & 1) offsetY = -offsetY;
    const Vec target{std::floor(enemy.pos.x) + offsetX + .5f, std::floor(enemy.pos.y) + offsetY + .5f};
    if (grid.walkable(target, rule) && !grid.path(enemy.pos, target, false, rule).empty()) return target;
    return std::nullopt;
}
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid, MovementCollisionRule rule) {
    const int fromX = int(std::floor(enemy.pos.x)), fromY = int(std::floor(enemy.pos.y));
    const int targetX = int(std::floor(target.x)), targetY = int(std::floor(target.y));
    const Vec destination{float(fromX + ((fromX > targetX) - (fromX < targetX)) * distance) + .5f,
                          float(fromY + ((fromY > targetY) - (fromY < targetY)) * distance) + .5f};
    if (!grid.walkable(destination, rule)) return false;
    auto route = grid.path(enemy.pos, destination, false, rule);
    if (route.empty()) return false;
    monsterStopApproach(enemy);
    enemy.route = std::move(route);
    enemy.aiEscaping = true;
    enemy.rethink = 0;
    return true;
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
