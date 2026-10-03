#include "gameplay/monsters/state.hpp"
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
    if (enemy.aiPhase == 1) {
        enemy.aiAlerted = false;
        if (life > 75) {
            enemy.aiPhase = 0;
            if (!roll(rules.params[2])) {
                enemy.aiAdvanceRemaining = 6;
                return ArachAction::Circle;
            }
            enemy.aiPhase = 2;
            return ArachAction::Approach;
        }
        if (inCombat && rules.params[0] > 25 && roll(rules.params[0] - 25))
            return ArachAction::Attack;
        if (distance < float(rules.params[3]) || enemy.aiRetaliate) {
            enemy.aiRetaliate = false;
            enemy.aiAdvanceRemaining = 4;
            return ArachAction::Retreat;
        }
        enemy.aiPhase = 0;
        enemy.aiAdvanceRemaining = 12;
        return ArachAction::Circle;
    }
    if (!inCombat) {
        if (enemy.aiRetaliate || enemy.aiAlerted) {
            enemy.aiRetaliate = false;
            enemy.aiAlerted = true;
            return ArachAction::Approach;
        }
        enemy.aiLoop = enemy.aiLoop >= 20 ? 0 : enemy.aiLoop + 1;
        if (enemy.aiLoop == 1 && roll(rules.params[2])) {
            enemy.aiAlerted = true;
            return ArachAction::Approach;
        }
        if (roll(20)) return ArachAction::Wander;
        enemy.aiWait = 15.f / 25.f;
        return ArachAction::Idle;
    }
    enemy.aiPhase = 2;
    if (roll(rules.params[0])) return ArachAction::Attack;
    if (life < rules.params[4]) {
        enemy.aiPhase = 1;
        if (enemy.webAuraRemaining == 0) return ArachAction::Web;
        enemy.aiAdvanceRemaining = 8;
        return ArachAction::Retreat;
    }
    if (roll(rules.params[1])) {
        enemy.aiAdvanceRemaining = 4;
        return ArachAction::Circle;
    }
    enemy.aiWait = 15.f / 25.f;
    return ArachAction::Idle;
}
} // namespace d2x
