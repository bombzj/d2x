#include "quill_rat_ai.hpp"
#include "monster_wander.hpp"

namespace d2x {
bool quillRatShoots(Enemy &enemy, const MonsterAiProfile &rules) {
    return monsterAiRandom(enemy) % 100 < unsigned(rules.params[1]);
}
} // namespace d2x
