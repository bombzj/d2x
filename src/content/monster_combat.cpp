#include "monster_combat.hpp"
#include <cstdint>

namespace d2x {
std::optional<MonsterNormalCombat> loadMonsterNormalCombat(const DataTable &stats, size_t row,
                                                          const DataTable *levels) {
    if (stats.number(row, "rangedtype").value_or(0) != 0)
        return std::nullopt;
    const auto level = stats.number(row, "Level");
    const auto lowLife = stats.number(row, "minHP");
    const auto highLife = stats.number(row, "maxHP");
    const auto lowDamage = stats.number(row, "A1MinD");
    const auto highDamage = stats.number(row, "A1MaxD");
    if (!level || *level < 1 || !lowLife || !highLife || !lowDamage || !highDamage ||
        *lowLife <= 0 || *highLife < *lowLife || *lowDamage < 0 || *highDamage < *lowDamage)
        return std::nullopt;
    int lifeRatio = 100, damageRatio = 100;
    if (stats.number(row, "noRatio").value_or(0) == 0) {
        if (!levels) return std::nullopt;
        size_t levelRow = 0;
        for (; levelRow < levels->rows().size(); ++levelRow)
            if (levels->number(levelRow, "Level") == level) break;
        if (levelRow == levels->rows().size()) return std::nullopt;
        auto life = levels->number(levelRow, "L-HP");
        auto damage = levels->number(levelRow, "L-DM");
        if (!life || !damage || *life <= 0 || *damage < 0) return std::nullopt;
        lifeRatio = *life;
        damageRatio = *damage;
    }
    auto scaled = [](int value, int ratio) { return int64_t(value) * ratio / 100; };
    const auto minLife = scaled(*lowLife, lifeRatio);
    const auto maxLife = scaled(*highLife, lifeRatio);
    const auto minDamage = scaled(*lowDamage, damageRatio);
    const auto maxDamage = scaled(*highDamage, damageRatio);
    if (minLife < 1 || maxLife > (1 << 23) - 1 || minDamage < 0 || maxDamage > 1000000)
        return std::nullopt;
    return MonsterNormalCombat{int(minLife), int(maxLife), int(minDamage), int(maxDamage)};
}
} // namespace d2x
