#include "quill_rat_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
bool quillRatShoots(Enemy &enemy, const MonsterAiProfile &rules) {
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[1]);
}
bool quillRatStartRetreat(Enemy &enemy, Vec target, int distance, const Grid &grid) {
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
