#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include "resources/anim_data.hpp"
#include <optional>

namespace d2x {
std::optional<MonsterAttackTiming> loadMonsterAttackTiming(const AnimDataTable &animations,
                                                           const MonsterDefinition &monster, int mode);
} // namespace d2x
