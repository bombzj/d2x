#pragma once
#include "gameplay/monsters/ai_spec.hpp"
#include <functional>

namespace d2x {
struct Enemy;
enum class VampireAction { Approach, Attack, CastFirst, CastFourth, Retreat, Circle, Idle };
VampireAction vampireThink(Enemy &enemy, const MonsterAiProfile &rules,
                           float distance, bool inCombat, float spellDistance,
                           const std::function<bool()> &retreat);
} // namespace d2x
