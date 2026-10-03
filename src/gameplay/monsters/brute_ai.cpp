#include "gameplay/monsters/state.hpp"
#include "brute_ai.hpp"
#include "monster_wander.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
float bruteWalkMultiplier(const Enemy &enemy) {
    if (enemy.maxHp <= 0) return 1.f;
    const int lifePercent = std::clamp(int(enemy.hp * 100.f / enemy.maxHp), 40, 100);
    return 1.f + float(100 - lifePercent) / 100.f;
}
BruteCombat bruteCombat(Enemy &enemy, const MonsterAiProfile &rules) {
    if (enemy.aiWait > 0) return BruteCombat::Idle;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return BruteCombat::Attack;
    if (monsterAiRandom(enemy) % 100 < unsigned(rules.params[2])) return BruteCombat::Circle;
    enemy.aiWait = 15.f / 25.f;
    return BruteCombat::Idle;
}

} // namespace d2x
