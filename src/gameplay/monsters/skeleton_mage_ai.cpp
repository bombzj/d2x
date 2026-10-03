#include "gameplay/model/state.hpp"
#include "skeleton_mage_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>

namespace d2x {
SkeletonMageAction skeletonMageThink(Enemy &enemy, const MonsterAiProfile &rules,
                                     float distance, bool clear) {
    if (enemy.aiWait > 0) return SkeletonMageAction::Idle;
    auto approach = [&] {
        monsterStartApproach(enemy, std::max(0, rules.params[1] - 1), 85, false);
        return SkeletonMageAction::Approach;
    };
    if (clear) {
        if (distance > float(rules.params[1]) &&
            monsterAiRandom(enemy) % 100 < unsigned(rules.params[2]))
            return approach();
        if (distance <= float(rules.params[3]) &&
            monsterAiRandom(enemy) % 100 < unsigned(rules.params[4]))
            return SkeletonMageAction::Retreat;
        if (distance < float(rules.params[5]) &&
            monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]))
            return SkeletonMageAction::Fire;
    }
    if (distance > float(rules.params[1]) &&
        monsterAiRandom(enemy) % 100 < unsigned(rules.params[2]))
        return approach();
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[6]))
        return SkeletonMageAction::Circle;
    enemy.aiWait = float(rules.params[7]) / 25.f;
    return SkeletonMageAction::Idle;
}
} // namespace d2x
