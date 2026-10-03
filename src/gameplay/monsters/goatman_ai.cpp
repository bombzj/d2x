#include "gameplay/monsters/state.hpp"
#include "goatman_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
namespace {
bool decide(Enemy &enemy, int chance, int stallFrames) {
    if (enemy.aiWait > 0) return false;
    if (monsterAiRandom(enemy) % 100 < unsigned(chance)) return true;
    enemy.aiWait = float(stallFrames) / 25.f;
    return false;
}
} // namespace

bool goatmanApproaches(Enemy &enemy, const MonsterAiProfile &rules) {
    return decide(enemy, rules.params[0], rules.params[1]);
}
bool goatmanAttacks(Enemy &enemy, const MonsterAiProfile &rules) {
    return decide(enemy, rules.params[2], rules.params[1]);
}
} // namespace d2x
