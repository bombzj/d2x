#include "resources/archive.hpp"
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
                                                           std::string_view weapon, int impactFlag) {
    if (mode != 1 && mode != 2) return std::nullopt;
    return loadMonsterActionTiming(animations, token, "a" + std::to_string(mode), weapon,
                                   impactFlag);
}
std::optional<MonsterAttackTiming> loadMonsterActionTiming(const AnimDataTable &animations,
                                                           std::string_view token,
                                                           std::string_view mode,
                                                           std::string_view weapon, int impactFlag) {
    std::string key = std::string(token) + std::string(mode) + std::string(weapon);
    for (auto &ch : key) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *record = animations.find(key);
    if (!record || record->frames == 0 || record->frames > 144 ||
        record->speed <= 0 || record->speed > 65535)
        return std::nullopt;
    const int frames = int(record->frames);
    int impact = -1;
    for (int index = 0; index < frames; ++index)
        if (record->frameFlags[index] == impactFlag) {
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
std::optional<MonsterAttackTiming> loadMonsterSequenceTiming(
    const AnimDataTable &animations, const DataTable &sequences,
    std::string_view sequence, std::string_view token,
    std::string_view mode, std::string_view weapon, int event) {
    const auto motion = loadMonsterMotionTiming(animations, token, mode, weapon);
    if (!motion) return std::nullopt;
    int sequenceFrames = 0, impactFrame = -1;
    std::string sequenceMode(mode);
    for (char &value : sequenceMode)
        value = char(std::toupper(static_cast<unsigned char>(value)));
    for (size_t row = 0; row < sequences.rows().size(); ++row) {
        if (sequences.value(row, "sequence") != sequence ||
            sequences.value(row, "mode") != sequenceMode) continue;
        const auto frame = sequences.number(row, "frame");
        if (!frame || *frame != sequenceFrames) return std::nullopt;
        if (sequences.number(row, "event").value_or(0) == event)
            impactFrame = *frame;
        ++sequenceFrames;
    }
    if (sequenceFrames <= 0 || sequenceFrames > 144 ||
        impactFrame < 0 || impactFrame >= sequenceFrames) return std::nullopt;
    const float frameTime = motion->duration / float(motion->frames);
    return MonsterAttackTiming{frameTime * sequenceFrames,
                               frameTime * impactFrame, motion->frames, sequenceFrames};
}
} // namespace d2x
