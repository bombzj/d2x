#include "monster_difficulty_combat.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace d2x {
std::optional<MonsterCombatProfile> loadMonsterCombatProfile(
    const DataTable &stats, size_t row, const DataTable &levels, int difficulty, int areaLevel, std::optional<int> forcedLevel,bool ladderStats,MonsterStatPurpose purpose) {
    const bool npc=purpose==MonsterStatPurpose::NativeNpc;
    if (difficulty < 0 || difficulty > 2 || areaLevel < (npc?0:1)) return std::nullopt;
    static constexpr std::array suffix{"", "(N)", "(H)"};
    const std::string ending = suffix[size_t(difficulty)];
    // Compiled MonStats blanks are zero. Service NPCs are living native
    // monsters with level/maxhp zero, rather than fabricated level-1 actors.
    const auto nativeLevel = npc?std::optional{stats.number(row,"Level"+ending).value_or(0)}:stats.number(row,"Level"+ending);
    const bool noRatio = stats.number(row, "noRatio").value_or(0) != 0;
    if (!nativeLevel || *nativeLevel < (npc?0:1)) return std::nullopt;
    const bool boss = stats.number(row, "boss").value_or(0) != 0;
    const int level = forcedLevel.value_or(difficulty == 0 || noRatio || boss ? *nativeLevel : areaLevel);
    size_t levelRow = 0;
    for (; levelRow < levels.rows().size(); ++levelRow)
        if (levels.number(levelRow, "Level") == level) break;
    if (levelRow == levels.rows().size()) return std::nullopt;
    auto scale = [&](std::string_view field, std::string_view ratioField) -> std::optional<int> {
        const auto source = npc?std::optional{stats.number(row,std::string(field)+ending).value_or(0)}:stats.number(row,std::string(field)+ending);
        if (!source || *source < 0) return std::nullopt;
        if (noRatio) return source;
        const auto ratioName=ladderStats?ratioField:ratioField.substr(2);
        const auto ratio = levels.number(levelRow, std::string(ratioName) + ending);
        if (!ratio || *ratio < 0) return std::nullopt;
        return nativePercentage(*ratio,*source,100);
    };
    auto lowHp = scale(difficulty == 0 ? "minHP" : "MinHP", "L-HP");
    auto highHp = scale(difficulty == 0 ? "maxHP" : "MaxHP", "L-HP");
    auto lowA1 = scale("A1MinD", "L-DM");
    auto highA1 = scale("A1MaxD", "L-DM");
    auto lowA2 = scale("A2MinD", "L-DM");
    auto highA2 = scale("A2MaxD", "L-DM");
    auto attack1 = scale("A1TH", "L-TH");
    auto attack2 = scale("A2TH", "L-TH");
    auto defense = scale("AC", "L-AC");
    auto critical = stats.number(row, "Crit");
    auto regen = stats.number(row, "DamageRegen");
    if (!lowHp || !highHp || *lowHp < (npc?0:1) || *highHp < *lowHp ||
        *highHp > (1 << 23) - 1 || (critical && (*critical < 0 || *critical > 100)) ||
        (regen && (*regen < 0 || *regen > 4096)))
        return std::nullopt;
    MonsterCombatProfile result;
    result.level = level;
    result.damage.minLife = *lowHp;
    result.damage.maxLife = *highHp;
    if (lowA1 && highA1 && *highA1 >= *lowA1 && *highA1 <= 1000000)
        result.damage.attack1Damage = std::pair{*lowA1, *highA1};
    if (lowA2 && highA2 && *highA2 >= *lowA2 && *highA2 <= 1000000)
        result.damage.attack2Damage = std::pair{*lowA2, *highA2};
    result.attack1Rating = attack1;
    result.attack2Rating = attack2;
    result.defense = defense;
    result.criticalChance = critical.value_or(0);
    result.damageRegen = regen.value_or(0);
    static constexpr std::array resistanceFields{"ResDm", "ResMa", "ResFi", "ResLi", "ResCo", "ResPo"};
    for (size_t index = 0; index < resistanceFields.size(); ++index) {
        const int value = stats.number(row, std::string(resistanceFields[index]) + ending).value_or(0);
        // MonStats stores signed 16-bit resistances. ArcaneTower's native
        // poison resistance is 1000; retaining it preserves its immunity.
        if (value < -100 || value > std::numeric_limits<int16_t>::max()) return std::nullopt;
        result.resistances[index] = value;
    }
    for (size_t index = 0; index < result.damage.elements.size(); ++index) {
        const auto prefix = "El" + std::to_string(index + 1);
        const std::string mode(stats.value(row, prefix + "Mode"));
        const std::string type(stats.value(row, prefix + "Type"));
        const int chance = stats.number(row, prefix + "Pct" + ending).value_or(0);
        if (chance < 0 || chance > 100) return std::nullopt;
        if (mode.empty() || type.empty() || chance == 0) continue;
        auto minimum = scale(prefix + "MinD", "L-DM");
        auto maximum = scale(prefix + "MaxD", "L-DM");
        if (type == "stun" && !minimum && !maximum)
            minimum = maximum = 0;
        const int duration = stats.number(row, prefix + "Dur" + ending).value_or(0);
        // A few shipped rows advertise an element but leave its damage blank.
        // Keep their life, physical damage and resistances available.
        if (!minimum || !maximum) continue;
        if (*minimum < 0 || *maximum < *minimum ||
            *maximum > 1000000 || duration < 0 || duration > 1000000)
            return std::nullopt;
        result.damage.elements[index] = MonsterElementAttack{
            mode, type, chance, *minimum, *maximum, duration};
    }
    return result;
}
} // namespace d2x
