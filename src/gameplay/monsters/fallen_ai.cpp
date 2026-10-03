#include "gameplay/units/movement.hpp"
#include "gameplay/monsters/state.hpp"
#include "fallen_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
FallenMovement fallenMovement(Enemy &enemy, const MonsterAiProfile &rules, float distance) {
    if (enemy.aiWait > 0) return FallenMovement::Idle;
    if (enemy.aiCommanded) return FallenMovement::Approach;
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

FallenCombat fallenCombat(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return FallenCombat::Idle;
    if (enemy.aiAlerted && !enemy.aiCommanded) {
        enemy.aiAlerted = false;
        return FallenCombat::Attack;
    }
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return FallenCombat::Attack;
    if (enemy.aiCommanded) {
        enemy.aiWait = 5.f / 25.f;
        return FallenCombat::Idle;
    }
    if (monsterAiRandom(enemy) % 100 < 30) return FallenCombat::Shout;
    enemy.aiWait = 10.f / 25.f;
    return FallenCombat::Idle;
}

bool fallenStartEscape(Enemy &enemy, Vec player, const Grid &grid, MovementCollisionRule rule) {
    if (!monsterStartRetreat(enemy, player, 12, grid, rule)) return false;
    enemy.aiPursuing = false;
    enemy.aiCommanded = false;
    enemy.aiAlerted = true;
    enemy.aiWait = 0;
    return true;
}

void fallenAdvanceEscape(Enemy &enemy, const Grid &grid, float speed, float dt, MovementCollisionRule rule) {
    const auto result = advanceRouteMovement(enemy.pos, enemy.route, speed, dt, .25f,
        [&](Vec from, Vec to) { return grid.segment(from, to, {}, rule); });
    if (result != RouteMovement::Moving) enemy.aiEscaping = false;
}
} // namespace d2x
