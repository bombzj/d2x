#pragma once
#include "gameplay/monsters/animation_spec.hpp"
#include "resources/anim_data.hpp"
#include "resources/data_table.hpp"
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
std::optional<MonsterAttackTiming> loadMonsterSequenceTiming(
    const AnimDataTable &animations, const DataTable &sequences,
    std::string_view sequence, std::string_view token,
    std::string_view mode, std::string_view weapon, int event);
} // namespace d2x
