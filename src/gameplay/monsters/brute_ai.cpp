#include "brute_ai.hpp"
#include <algorithm>

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy) {
    if (enemy.maxHp <= 0) return 1.f;
    const int lifePercent = std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 40, 100);
    return 1.f + float(100 - lifePercent) / 100.f;
}
int bruteAttackMode(Enemy &enemy, const MonsterAiProfile &rules) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom) % 100 < unsigned(rules.params[3]) ? 1 : 2;
}
} // namespace d2x
