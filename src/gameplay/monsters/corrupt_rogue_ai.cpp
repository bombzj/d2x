#include "gameplay/model/state.hpp"
#include "corrupt_rogue_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
CorruptRogueMovement corruptRogueMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                         float distance, int difficulty) {
    if (enemy.aiWait > 0) return CorruptRogueMovement::Idle;
    if (distance <= float(20 - 3 * difficulty)) {
        if (monsterAiRandom(enemy) % 100 >= unsigned(rules.params[0])) {
            enemy.aiWait = float(rules.params[1]) / 25.f;
            return CorruptRogueMovement::Idle;
        }
        if (monsterAiRandom(enemy) % 100 >= unsigned(rules.params[4]))
            return CorruptRogueMovement::Walk;
    }
    return CorruptRogueMovement::Run;
}

bool corruptRogueAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return true;
    enemy.aiWait = float(rules.params[1]) / 25.f;
    return false;
}
} // namespace d2x
