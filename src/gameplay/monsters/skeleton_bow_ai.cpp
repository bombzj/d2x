#include "skeleton_bow_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
SkeletonBowAction skeletonBowThink(Enemy &enemy, const MonsterAiProfile &rules,
                                   float distance, bool clear) {
    if (enemy.aiWait > 0) return SkeletonBowAction::Idle;
    if (enemy.aiAdvanceRemaining > 0 && distance > float(rules.params[4]))
        return SkeletonBowAction::Approach;
    enemy.aiAdvanceRemaining = 0;
    // D2MOO SkeletonBow: target within 20 tiles, then shoot or take a short step.
    if (clear && distance < 20.f) {
        if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]))
            return SkeletonBowAction::Shoot;
        if (distance > float(rules.params[4]) && monsterAiRandom(enemy) % 100 < 20u) {
            enemy.aiAdvanceRemaining = 3.f;
            return SkeletonBowAction::Approach;
        }
        enemy.aiWait = float(rules.params[1]) / 25.f;
        return SkeletonBowAction::Idle;
    }
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) {
        enemy.aiAdvanceRemaining = float(rules.params[3]);
        return SkeletonBowAction::Approach;
    }
    enemy.aiWait = 20.f / 25.f;
    return SkeletonBowAction::Idle;
}
} // namespace d2x
