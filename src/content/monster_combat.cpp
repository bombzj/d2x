#include "monster_combat.hpp"
#include <cstdint>

namespace d2x {
std::optional<MonsterNormalCombat> loadMonsterNormalCombat(const DataTable &stats, size_t row,
                                                          const DataTable *levels) {
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
    MonsterNormalCombat combat{int(minLife), int(maxLife), int(minDamage), int(maxDamage), std::nullopt};
    const auto lowDamage2 = stats.number(row, "A2MinD");
    const auto highDamage2 = stats.number(row, "A2MaxD");
    if (lowDamage2 && highDamage2 && *lowDamage2 >= 0 && *highDamage2 >= *lowDamage2) {
        const auto minimum = scaled(*lowDamage2, damageRatio);
        const auto maximum = scaled(*highDamage2, damageRatio);
        if (minimum >= 0 && maximum <= 1000000)
            combat.attack2Damage = std::pair{int(minimum), int(maximum)};
    }
    return combat;
}
} // namespace d2x
