#include "fallen_ai.hpp"
#include "monster_wander.hpp"

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
} // namespace d2x
