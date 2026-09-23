#include "skeleton_ai.hpp"
#include <cstdint>

namespace d2x {
namespace {
bool roll(Enemy &enemy, int chance) {
    enemy.combatRandom = uint64_t(uint32_t(enemy.combatRandom)) * 0x6ac690c5ULL +
                         (enemy.combatRandom >> 32);
    return uint32_t(enemy.combatRandom) % 100 < unsigned(chance);
}
void stall(Enemy &enemy, const MonsterAiProfile &rules) {
    enemy.aiWait = float(rules.params[1]) / 25.f;
}
} // namespace
bool skeletonApproaches(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (enemy.aiPursuing) return true;
    if (roll(enemy, rules.params[0])) {
        enemy.aiPursuing = true;
        return true;
    }
    stall(enemy, rules);
    return false;
}
bool skeletonAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    enemy.aiPursuing = false;
    if (enemy.aiWait > 0) return false;
    if (roll(enemy, rules.params[2])) return true;
    stall(enemy, rules);
    return false;
}
} // namespace d2x
