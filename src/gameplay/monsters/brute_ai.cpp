#include "brute_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy) {
    if (enemy.maxHp <= 0) return 1.f;
    const int lifePercent = std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 40, 100);
    return 1.f + float(100 - lifePercent) / 100.f;
}
BruteCombat bruteCombat(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return BruteCombat::Idle;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return BruteCombat::Attack;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return BruteCombat::Circle;
    enemy.aiWait = 15.f / 25.f;
    return BruteCombat::Idle;
}

bool bruteStartCircle(Enemy &enemy, Vec target, const Grid &grid) {
    // D2MOO's Brute fallback chooses MON_CIRCLE_CW or CCW around its target,
    // with distance 4. Choose a reachable point on that arc using this map grid.
    const Vec offset = enemy.pos - target;
    if (offset.length() == 0) return false;
    const Vec radial = offset.unit();
    const bool clockwise = (monsterAiRandom(enemy) & 255) < 128;
    const Vec tangent = clockwise ? Vec{-radial.y, radial.x} : Vec{radial.y, -radial.x};
    for (float angleStep : {1.f, .75f, .5f}) {
        const Vec direction = (radial + tangent * angleStep).unit();
        const Vec destination = target + direction * 4.f;
        if (!grid.walkable(destination)) continue;
        auto route = grid.path(enemy.pos, destination);
        if (route.empty()) continue;
        enemy.route = std::move(route);
        enemy.aiCircling = true;
        enemy.aiWait = 0;
        return true;
    }
    return false;
}

void bruteAdvanceCircle(Enemy &enemy, const Grid &grid, float speed, float dt) {
    while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
        enemy.route.pop_front();
    if (enemy.route.empty()) {
        enemy.aiCircling = false;
        return;
    }
    const Vec offset = enemy.route.front() - enemy.pos;
    const Vec next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
    if (!grid.segment(enemy.pos, next)) {
        enemy.route.clear();
        enemy.aiCircling = false;
        return;
    }
    enemy.pos = next;
    if ((enemy.route.front() - enemy.pos).length() < .25f) enemy.route.pop_front();
    if (enemy.route.empty()) enemy.aiCircling = false;
}
} // namespace d2x
