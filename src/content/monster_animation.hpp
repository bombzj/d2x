#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include "resources/anim_data.hpp"
#include <optional>

namespace d2x {
struct MonsterMotionTiming { float duration = 0; int frames = 0; };
std::optional<MonsterMotionTiming> loadMonsterMotionTiming(const AnimDataTable &animations,
                                                          const MonsterDefinition &monster,
                                                          std::string_view mode);
std::optional<MonsterAttackTiming> loadMonsterAttackTiming(const AnimDataTable &animations,
                                                           const MonsterDefinition &monster, int mode);
} // namespace d2x
