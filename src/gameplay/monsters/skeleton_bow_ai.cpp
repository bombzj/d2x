#include "skeleton_bow_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
SkeletonBowAction skeletonBowThink(Enemy &enemy, const MonsterAiProfile &rules,
                                   float distance, bool clear) {
    if (enemy.aiWait > 0) return SkeletonBowAction::Idle;
    if (clear && distance < 20.f) {
        if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[0]))
            return SkeletonBowAction::Shoot;
        if (monsterAiRandom(enemy) % 100 < 20u) return SkeletonBowAction::Circle;
        enemy.aiWait = float(rules.params[1]) / 25.f;
        return SkeletonBowAction::Idle;
    }
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) {
        return SkeletonBowAction::Approach;
    }
    enemy.aiWait = 20.f / 25.f;
    return SkeletonBowAction::Idle;
}
} // namespace d2x
