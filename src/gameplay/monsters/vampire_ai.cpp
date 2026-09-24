#include "vampire_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>

namespace d2x {
namespace {
bool roll(Enemy &enemy, int chance) {
    return monsterAiRandom(enemy) % 100 < unsigned(chance);
}
VampireAction ordinarySpell(Enemy &enemy) {
    return roll(enemy, 50) ? VampireAction::CastFirst : VampireAction::CastFourth;
}
} // namespace
VampireAction vampireThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool inCombat) {
    if (enemy.aiWait > 0) return VampireAction::Idle;
    if (distance < 30.f) enemy.aiLoop = std::max(enemy.aiLoop, int(distance));
    const int lifePercent = enemy.maxHp > 0 ? int(enemy.hp * 100.f / enemy.maxHp) : 100;
    const bool ordinarySpells = (rules.params[4] & 1) != 0;
    if (enemy.aiPhase == 2) {
        if (lifePercent >= 75) {
            enemy.aiPhase = 1;
            return VampireAction::Approach;
        }
        if (distance < 14.f || distance <= float(enemy.aiLoop)) return VampireAction::Retreat;
        if (distance >= float(rules.params[2]) || !roll(enemy, rules.params[1])) {
            enemy.aiWait = 15.f / 25.f;
            return VampireAction::Idle;
        }
        if (ordinarySpells && distance <= 20.f) return ordinarySpell(enemy);
        return VampireAction::Circle;
    }
    if (lifePercent < 33) {
        enemy.aiPhase = 2;
        return VampireAction::Retreat;
    }
    if (inCombat) {
        enemy.aiPhase = 1;
        if (roll(enemy, rules.params[0])) {
            if (ordinarySpells && distance <= 20.f && roll(enemy, 30))
                return ordinarySpell(enemy);
            return VampireAction::Attack;
        }
        if (roll(enemy, 33)) return VampireAction::Circle;
        enemy.aiWait = 10.f / 25.f;
        return VampireAction::Idle;
    }
    if (distance >= float(rules.params[2])) {
        if (enemy.aiPhase == 1) return VampireAction::Approach;
        enemy.aiWait = 15.f / 25.f;
        return VampireAction::Idle;
    }
    enemy.aiPhase = 1;
    if (roll(enemy, rules.params[1])) {
        if (!ordinarySpells || distance > 20.f) return VampireAction::Approach;
        if (roll(enemy, 25)) return VampireAction::Circle;
        return ordinarySpell(enemy);
    }
    if (distance > 20.f) return VampireAction::Approach;
    if (distance < 9.f && roll(enemy, 50)) return VampireAction::Retreat;
    if (roll(enemy, 50)) return VampireAction::Circle;
    enemy.aiWait = 10.f / 25.f;
    return VampireAction::Idle;
}
} // namespace d2x
