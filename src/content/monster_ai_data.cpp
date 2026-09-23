#include "monster_ai_data.hpp"
#include <string>

namespace d2x {
std::optional<MonsterAiProfile> loadMonsterAiProfile(const DataTable &stats, size_t row,
                                                     std::string_view ai, int difficulty) {
    if (difficulty < 0 || difficulty > 2) return std::nullopt;
    MonsterAiProfile profile;
    if (ai == "Skeleton") profile.kind = MonsterAiKind::Skeleton;
    else if (ai == "Brute") profile.kind = MonsterAiKind::Brute;
    else if (ai == "Zombie") profile.kind = MonsterAiKind::Zombie;
    else return std::nullopt;
    const std::string suffix = difficulty == 0 ? "" : difficulty == 1 ? "(N)" : "(H)";
    for (int index = 0; index < 8; ++index) {
        auto value = stats.number(row, "aip" + std::to_string(index + 1) + suffix);
        if (value && (*value < 0 || *value > 65535)) return std::nullopt;
        profile.params[index] = value.value_or(0);
    }
    auto percentage = [&](int index) { return profile.params[index] <= 100; };
    if (profile.kind == MonsterAiKind::Skeleton &&
        (!percentage(0) || !percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Brute &&
        (!percentage(2) || !percentage(3))) return std::nullopt;
    if (profile.kind == MonsterAiKind::Zombie &&
        (!percentage(0) || !percentage(3))) return std::nullopt;
    return profile;
}
} // namespace d2x
