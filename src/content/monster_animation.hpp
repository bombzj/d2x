#pragma once
#include "gameplay/monsters/monster_spawn.hpp"
#include "resources/anim_data.hpp"
#include <optional>

namespace d2x {
struct MonsterMotionTiming { float duration = 0; int frames = 0; };
std::string monsterModeWeapon(const Archives &archives, std::string_view token,
                              std::string_view mode, std::string_view baseWeapon);
std::optional<MonsterMotionTiming> loadMonsterMotionTiming(const AnimDataTable &animations,
                                                          std::string_view token,
                                                          std::string_view mode,
                                                          std::string_view weapon);
std::optional<MonsterAttackTiming> loadMonsterAttackTiming(const AnimDataTable &animations,
                                                           std::string_view token, int mode,
                                                           std::string_view weapon, int impactFlag = 1);
std::optional<MonsterAttackTiming> loadMonsterActionTiming(const AnimDataTable &animations,
                                                           std::string_view token,
                                                           std::string_view mode,
                                                           std::string_view weapon, int impactFlag);
} // namespace d2x
