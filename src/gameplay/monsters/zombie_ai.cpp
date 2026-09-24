#include "zombie_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance) {
    if (enemy.aiPursuing) return true;
    if (distance < float(rules.params[1]) && enemy.aiWait <= 0) {
        enemy.aiPursuing = monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]);
        if (!enemy.aiPursuing) enemy.aiWait = 10.f / 25.f;
    }
    return enemy.aiPursuing;
}
} // namespace d2x
