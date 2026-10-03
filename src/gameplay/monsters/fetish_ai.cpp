#include "gameplay/model/state.hpp"
#include "fetish_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>

namespace d2x {
FetishAction fetishThink(Enemy &enemy, const MonsterAiProfile &rules,
                         float distance, bool inCombat, int targetLifePercent) {
    if (enemy.aiWait > 0) return FetishAction::Idle;
    if (enemy.aiPhase == 0) {
        if (!inCombat) return FetishAction::Approach;
        enemy.aiPhase = 1;
        enemy.aiLoop = 0;
    } else if (enemy.aiPhase == 1) {
        enemy.aiLoop = std::min(enemy.aiLoop + 1, rules.params[2] + 1);
        if (enemy.aiLoop > rules.params[2] && targetLifePercent > rules.params[3]) {
            enemy.aiPhase = 2;
            enemy.aiLoop = 0;
            return FetishAction::Retreat;
        }
        if (!inCombat) return FetishAction::Approach;
    } else if (enemy.aiPhase == 2) {
        if (distance <= 12.f) return FetishAction::Retreat;
        if (++enemy.aiLoop > 1) {
            enemy.aiPhase = 0;
            enemy.aiLoop = 0;
        }
        if (monsterAiRandom(enemy) % 100 < 20) return FetishAction::Circle;
        enemy.aiWait = 10.f / 25.f;
        return FetishAction::Idle;
    }
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]))
        return FetishAction::Attack;
    enemy.aiWait = float(rules.params[1]) / 25.f;
    return FetishAction::Idle;
}

void fetishRetreatFailed(Enemy &enemy) {
    enemy.aiPhase = 0;
    enemy.aiLoop = 0;
    enemy.aiWait = 10.f / 25.f;
}
} // namespace d2x
