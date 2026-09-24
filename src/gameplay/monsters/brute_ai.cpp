#include "brute_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy) {
    if (enemy.maxHp <= 0) return 1.f;
    const int lifePercent = std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 40, 100);
    return 1.f + float(100 - lifePercent) / 100.f;
}
bool bruteAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return true;
    // Side stepping after the second roll needs the original movement parameters.
    enemy.aiWait = 15.f / 25.f;
    return false;
}
} // namespace d2x
