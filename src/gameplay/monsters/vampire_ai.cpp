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
                           float distance, bool inCombat, float spellDistance,
                           const std::function<bool()> &retreat) {
    if (enemy.aiWait > 0) return VampireAction::Idle;
    const int lifePercent = enemy.maxHp > 0 ? int(enemy.hp * 100.f / enemy.maxHp) : 100;
    const bool ordinarySpells = (rules.params[4] & 1) != 0;
    if (enemy.aiRetaliate) {
        enemy.aiRetaliate = false;
        if (!enemy.aiPhase) enemy.aiPhase = 1;
        if (distance < 30.f) enemy.aiLoop = std::max(enemy.aiLoop, int(distance));
        if (inCombat) return ordinarySpells && roll(enemy, 31) ? ordinarySpell(enemy) : VampireAction::Attack;
    }
    if (enemy.aiPhase == 2) {
        if (lifePercent >= 75) {
            enemy.aiPhase = 1;
            return VampireAction::Approach;
        }
        if ((distance < 14.f || distance <= float(enemy.aiLoop)) && retreat()) return VampireAction::Idle;
        if (distance >= float(rules.params[2]) || !roll(enemy, rules.params[1])) {
            enemy.aiWait = 15.f / 25.f;
            return VampireAction::Idle;
        }
        if (ordinarySpells && spellDistance >= 0 && spellDistance <= 20.f) return ordinarySpell(enemy);
        return VampireAction::Circle;
    }
    if (lifePercent < 33) {
        enemy.aiPhase = 2;
        if (retreat()) return VampireAction::Idle;
    }
    if (inCombat) {
        enemy.aiPhase = 1;
        if (roll(enemy, rules.params[0])) {
            if (ordinarySpells && spellDistance >= 0 && spellDistance <= 20.f && roll(enemy, 31))
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
        if (!ordinarySpells || spellDistance < 0 || spellDistance > 20.f) return VampireAction::Approach;
        if (monsterAiRandom(enemy) % 100 >= 75) return VampireAction::Circle;
        return ordinarySpell(enemy);
    }
    if (distance > 20.f) return VampireAction::Approach;
    if (distance < 9.f && roll(enemy, 50)) {
        retreat();
        return VampireAction::Idle;
    }
    if (roll(enemy, 50)) return VampireAction::Circle;
    enemy.aiWait = 10.f / 25.f;
    return VampireAction::Idle;
}
} // namespace d2x
