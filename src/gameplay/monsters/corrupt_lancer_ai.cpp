#include "gameplay/monsters/state.hpp"
#include "corrupt_lancer_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
CorruptLancerMovement corruptLancerMovement(Enemy &enemy, const MonsterAiProfile &rules,
                                           float distance) {
    if (enemy.aiWait > 0) return CorruptLancerMovement::Idle;
    // D2MOO's CorruptLancer always runs from beyond aip5 and remembers that
    // charge so its next close combat decision attacks without another roll.
    if (distance > float(rules.params[4])) {
        enemy.aiCharged = true;
        return CorruptLancerMovement::Run;
    }
    if (monsterAiRandom(enemy) % 100 >= unsigned(rules.params[0])) {
        enemy.aiWait = float(rules.params[2]) / 25.f;
        return CorruptLancerMovement::Idle;
    }
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[3])
        ? CorruptLancerMovement::Run : CorruptLancerMovement::Walk;
}

bool corruptLancerAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return false;
    if (enemy.aiCharged) {
        enemy.aiCharged = false;
        return true;
    }
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[1])) return true;
    enemy.aiWait = float(rules.params[2]) / 25.f;
    return false;
}
} // namespace d2x
