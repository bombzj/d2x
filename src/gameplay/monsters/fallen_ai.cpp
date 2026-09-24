#include "fallen_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
FallenMovement fallenMovement(Enemy &enemy, const MonsterAiProfile &rules, float distance) {
    if (enemy.aiWait > 0) return FallenMovement::Idle;
    if (distance <= float(rules.params[1])) {
        enemy.aiPursuing = true;
        return FallenMovement::Approach;
    }
    if (enemy.aiPursuing) {
        enemy.route.clear();
        enemy.aiPursuing = false;
    }
    if (!enemy.route.empty() || monsterAiRandom(enemy) % 100 < 30)
        return FallenMovement::Wander;
    enemy.aiWait = 10.f / 25.f;
    return FallenMovement::Idle;
}

bool fallenAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return true;
    // The original's Skill2 shout is deferred with group-command handling.
    enemy.aiWait = 10.f / 25.f;
    return false;
}

bool fallenStartEscape(Enemy &enemy, Vec player, const Grid &grid) {
    // D2MOO's Fallen AI calls Escape(..., 12, 1) when it sees a fresh corpse.
    // Escape uses the sign of the vector away from the current target.
    const Vec away{float((enemy.pos.x > player.x) - (enemy.pos.x < player.x)),
                   float((enemy.pos.y > player.y) - (enemy.pos.y < player.y))};
    if (away.length() == 0) return false;
    for (int distance = 12; distance >= 1; --distance) {
        const Vec target = enemy.pos + away * float(distance);
        if (!grid.walkable(target)) continue;
        auto route = grid.path(enemy.pos, target);
        if (route.empty()) continue;
        enemy.route = std::move(route);
        enemy.aiEscaping = true;
        enemy.aiPursuing = false;
        enemy.aiWait = 0;
        return true;
    }
    return false;
}

void fallenAdvanceEscape(Enemy &enemy, const Grid &grid, float speed, float dt) {
    while (!enemy.route.empty() && (enemy.route.front() - enemy.pos).length() < .25f)
        enemy.route.pop_front();
    if (enemy.route.empty()) {
        enemy.aiEscaping = false;
        return;
    }
    const Vec offset = enemy.route.front() - enemy.pos;
    const Vec next = enemy.pos + offset.unit() * std::min(speed * dt, offset.length());
    if (!grid.segment(enemy.pos, next)) {
        enemy.route.clear();
        enemy.aiEscaping = false;
        return;
    }
    enemy.pos = next;
    if ((enemy.route.front() - enemy.pos).length() < .25f) enemy.route.pop_front();
    if (enemy.route.empty()) enemy.aiEscaping = false;
}
} // namespace d2x
