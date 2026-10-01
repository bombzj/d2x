#include "zombie_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool zombiePursues(Enemy &enemy, const MonsterAiProfile &rules, float distance,
                   bool forcedByLevel) {
    const bool alerted = enemy.aiRetaliate;
    enemy.aiRetaliate = false;
    return alerted || forcedByLevel || (distance < float(rules.params[1]) &&
        monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]));
}
} // namespace d2x
