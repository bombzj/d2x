#include "monster_animation.hpp"
#include <cctype>
#include <string>

namespace d2x {
std::string monsterModeWeapon(const Archives &archives, std::string_view token,
                              std::string_view mode, std::string_view baseWeapon) {
    const auto path = [&](std::string_view weapon) {
        return "data/global/monsters/" + std::string(token) + "/cof/" +
               std::string(token) + std::string(mode) + std::string(weapon) + ".cof";
    };
    if (!baseWeapon.empty() && archives.contains(path(baseWeapon)))
        return std::string(baseWeapon);
    if (baseWeapon != "hth" && archives.contains(path("hth"))) return "hth";
    return {};
}
std::optional<MonsterMotionTiming> loadMonsterMotionTiming(const AnimDataTable &animations,
                                                          std::string_view token,
                                                          std::string_view mode,
                                                          std::string_view weapon) {
    std::string key = std::string(token) + std::string(mode) + std::string(weapon);
    for (auto &ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *record = animations.find(key);
    if (!record || record->frames == 0 || record->frames > 144 ||
        record->speed <= 0 || record->speed > 65535)
        return std::nullopt;
    const float duration = float(record->frames * 256) / record->speed / 25.f;
    if (duration <= 0 || duration > 20) return std::nullopt;
    return MonsterMotionTiming{duration, int(record->frames)};
}
std::optional<MonsterAttackTiming> loadMonsterAttackTiming(const AnimDataTable &animations,
                                                           std::string_view token, int mode,
                                                           std::string_view weapon) {
    if (mode != 1 && mode != 2) return std::nullopt;
    std::string key = std::string(token) + "a" + std::to_string(mode) + std::string(weapon);
    for (auto &ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *record = animations.find(key);
    if (!record || record->frames == 0 || record->frames > 144 ||
        record->speed <= 0 || record->speed > 65535)
        return std::nullopt;
    const int frames = int(record->frames);
    int impact = -1;
    for (int index = 0; index < frames; ++index)
        if (record->frameFlags[index] == 1) {
            impact = index;
            break;
        }
    if (impact < 0) return std::nullopt;
    const float duration = float(frames * 256) / record->speed / 25.f;
    const float impactTime = float(impact * 256) / record->speed / 25.f;
    if (duration <= 0 || duration > 20 || impactTime >= duration)
        return std::nullopt;
    return MonsterAttackTiming{duration, impactTime, frames};
}
} // namespace d2x
