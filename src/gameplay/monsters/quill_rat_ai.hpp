#pragma once
#include "gameplay/monsters/ai_spec.hpp"

namespace d2x {
struct Enemy;
bool quillRatShoots(Enemy &enemy, const MonsterAiProfile &rules);
} // namespace d2x
