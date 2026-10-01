#include "bighead_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
BigheadAction bigheadThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool clear, bool inCombat) {
    if (enemy.aiWait > 0) return BigheadAction::Idle;
    const bool healthy = enemy.maxHp <= 0 ||
        enemy.hp * 100.f >= enemy.maxHp * float(rules.params[0]);
    if (healthy) {
        if (inCombat) return BigheadAction::Melee;
        if (clear && distance < 15.f &&
            monsterAiRandom(enemy) % 100 < unsigned(rules.params[2]))
            return BigheadAction::Fire;
        return BigheadAction::Approach;
    }
    if (distance < 3.f) return BigheadAction::Retreat;
    if (distance > 15.f) return BigheadAction::Approach;
    if (clear && monsterAiRandom(enemy) % 100 < unsigned(rules.params[3]))
        return BigheadAction::Fire;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[1]))
        return BigheadAction::Circle;
    enemy.aiWait = 10.f / 25.f;
    return BigheadAction::Idle;
}
} // namespace d2x
