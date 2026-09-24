#include "corrupt_archer_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool corruptArcherRetreats(Enemy &enemy, const MonsterAiProfile &rules) {
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[3]);
}
bool corruptArcherApproaches(Enemy &enemy, const MonsterAiProfile &rules, float distance) {
    return rules.params[7] > 0 && distance > float(rules.params[7]) &&
           monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]);
}
bool corruptArcherShoots(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[1])) return true;
    enemy.aiWait = float(rules.params[2]) / 25.f;
    return false;
}
} // namespace d2x
