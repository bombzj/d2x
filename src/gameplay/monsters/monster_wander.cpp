#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
uint32_t monsterAiRandom(Enemy &enemy) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom);
}

std::optional<Vec> monsterWanderTarget(Enemy &enemy, const Grid &grid, int radius) {
    if (radius <= 0) return std::nullopt;
    for (int attempt = 0; attempt < 4; ++attempt) {
        int x = radius, y = int(monsterAiRandom(enemy) % unsigned(radius));
        if (monsterAiRandom(enemy) & 1) std::swap(x, y);
        if (monsterAiRandom(enemy) & 1) x = -x;
        if (monsterAiRandom(enemy) & 1) y = -y;
        const Vec target = enemy.pos + Vec{float(x), float(y)};
        if (grid.walkable(target) && grid.segment(enemy.pos, target)) return target;
    }
    return std::nullopt;
}
bool monsterStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid) {
    const Vec offset = enemy.pos - target;
    if (offset.length() == 0) return false;
    const Vec away = offset.unit();
    const Vec tangent{-away.y, away.x};
    for (float side : {0.f, .5f, -.5f, 1.f, -1.f}) {
        const Vec destination = enemy.pos + (away + tangent * side).unit() *
                                float(std::max(distance, 1));
        if (!grid.walkable(destination)) continue;
        auto route = grid.path(enemy.pos, destination);
        if (route.empty()) continue;
        enemy.route = std::move(route);
        enemy.aiEscaping = true;
        enemy.rethink = 0;
        return true;
    }
    return false;
}
} // namespace d2x
