#include "monster_animation.hpp"
#include <cctype>
#include <string>

namespace d2x {
std::optional<MonsterAttackTiming> loadMonsterAttackTiming(const AnimDataTable &animations,
                                                           const MonsterDefinition &monster, int mode) {
    if (mode != 1 && mode != 2) return std::nullopt;
    std::string key = std::string(monster.token) + "a" + std::to_string(mode) + monster.weapon;
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
