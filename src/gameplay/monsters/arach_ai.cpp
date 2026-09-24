#include "arach_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>

namespace d2x {
ArachAction arachThink(Enemy &enemy, const MonsterAiProfile &rules,
                       float distance, bool inCombat) {
    if (enemy.aiWait > 0) return ArachAction::Idle;
    auto roll = [&](int chance) {
        return monsterAiRandom(enemy) % 100 < unsigned(chance);
    };
    const int life = enemy.maxHp > 0
        ? std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 0, 100) : 0;
    if (life > 75) enemy.aiPhase = 0;
    if (inCombat) {
        if (roll(enemy.aiPhase == 1 ? std::max(0, rules.params[0] - 25)
                                    : rules.params[0])) return ArachAction::Attack;
        if (life < rules.params[4] && enemy.webAuraRemaining == 0) {
            enemy.aiPhase = 1;
            return ArachAction::Web;
        }
        if (enemy.aiPhase == 1) return ArachAction::Retreat;
        if (roll(rules.params[1])) return ArachAction::Circle;
        enemy.aiWait = 15.f / 25.f;
        return ArachAction::Idle;
    }
    if (enemy.aiPhase == 1 && life <= 75 && distance < float(rules.params[3]))
        return ArachAction::Retreat;
    if (roll(rules.params[2])) return ArachAction::Approach;
    if (roll(20)) return ArachAction::Circle;
    enemy.aiWait = 15.f / 25.f;
    return ArachAction::Idle;
}
} // namespace d2x
