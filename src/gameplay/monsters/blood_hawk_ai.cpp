#include "gameplay/model/state.hpp"
#include "blood_hawk_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
BloodHawkAction bloodHawkThink(Enemy &enemy, const MonsterAiProfile &rules,
                               float distance, bool inCombat) {
    auto roll = [&](int chance) {
        return monsterAiRandom(enemy) % 100 < unsigned(chance);
    };
    if (enemy.aiCharged && inCombat) {
        enemy.aiCharged = false;
        return BloodHawkAction::Attack;
    }
    enemy.aiCharged = false;
    if (inCombat) {
        enemy.aiCharged = false;
        return roll(rules.params[2]) ? BloodHawkAction::Attack
                                     : BloodHawkAction::Retreat;
    }
    if (roll(rules.params[0])) {
        enemy.aiCharged = true;
        return BloodHawkAction::Charge;
    }
    enemy.aiCharged = false;
    if (distance <= 3.f) return BloodHawkAction::Retreat;
    return roll(rules.params[1]) ? BloodHawkAction::Circle
                                 : BloodHawkAction::Approach;
}
} // namespace d2x
