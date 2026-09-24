#include "wraith_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
namespace {
bool decide(Enemy &enemy, const MonsterAiProfile &rules, int chance) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(chance)) return true;
    enemy.aiWait = float(rules.params[1]) / 25.f;
    return false;
}
} // namespace

bool wraithApproaches(Enemy &enemy, const MonsterAiProfile &rules) {
    return decide(enemy, rules, rules.params[0]);
}

bool wraithAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    return decide(enemy, rules, rules.params[2]);
}
} // namespace d2x
