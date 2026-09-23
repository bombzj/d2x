#include "monster_experience.hpp"
#include <algorithm>
#include <array>

namespace d2x {
MonsterExperienceAward resolveMonsterExperience(const ClassicData &data, const MonsterCatalog &monsters,
                                               const WorldCatalog &world,
                                               const MonsterExperienceRequest &request) {
    MonsterExperienceAward result;
    auto defer = [&](std::string reason) {
        result.deferred = std::move(reason);
        return result;
    };
    if (data.profile != "lod-named-txt-v1" || request.difficulty < 0 || request.difficulty > 2 ||
        request.playerLevel < 1)
        return defer("Unsupported monster experience profile or difficulty");
    const auto *monster = monsters.find(request.identity.monster);
    if (!monster || !monster->hostile())
        return defer("Unknown or non-hostile monster identity");
    const auto &stats = data.tables.at("monstats");
    const auto &levels = data.tables.at("monlvl");
    const auto &experience = data.tables.at("experience");
    size_t row = 0;
    for (; row < stats.rows().size(); ++row)
        if (stats.value(row, "Id") == monster->id) break;
    if (row == stats.rows().size())
        return defer("Missing original monster experience row");
    const char *suffix[] = {"", "(N)", "(H)"};
    auto level = stats.number(row, std::string("Level") + suffix[request.difficulty]);
    auto percent = stats.number(row, std::string("Exp") + suffix[request.difficulty]);
    if (!level || !percent || *level < 1 || *percent < 0)
        return defer("Invalid original monster level or experience");
    const bool noRatio = stats.number(row, "noRatio").value_or(0) != 0;
    if (request.difficulty > 0 && !noRatio && !monster->boss) {
        auto area = world.levels().find(int(request.region));
        if (area == world.levels().end() || !area->second.population.supported)
            return defer("Missing area monster level");
        level = area->second.population.level[request.difficulty];
    }
    if (*level < 1 || *level > 99)
        return defer("Unsupported monster level");
    uint64_t base = unsigned(*percent);
    if (!noRatio) {
        size_t levelRow = 0;
        for (; levelRow < levels.rows().size(); ++levelRow)
            if (levels.number(levelRow, "Level") == level) break;
        if (levelRow == levels.rows().size())
            return defer("Missing original MonLvl row");
        auto ratio = levels.number(levelRow, std::string("L-XP") + suffix[request.difficulty]);
        if (!ratio || *ratio < 0)
            return defer("Invalid original MonLvl experience");
        base = uint64_t(*ratio) * base / 100;
    }
    int bonusLevel = 0, rankFactor = 1;
    switch (request.identity.rank) {
    case MonsterRank::Normal:
    case MonsterRank::Minion:
    case MonsterRank::Boss: break;
    case MonsterRank::Champion: bonusLevel = 2; rankFactor = 3; break;
    case MonsterRank::Unique: bonusLevel = 3; rankFactor = 5; break;
    case MonsterRank::SuperUnique: {
        const auto *unique = monsters.superUnique(request.identity.superUnique);
        if (!unique || unique->monster != monster->id)
            return defer("Missing super unique experience identity");
        for (int modifier : unique->modifiers)
            if (modifier == 4 || modifier == 16 || (modifier >= 36 && modifier <= 39))
                return defer("Super unique has unhandled experience modifier");
        bonusLevel = 3; rankFactor = 5;
        break;
    }
    }
    result.monsterLevel = *level + bonusLevel;
    uint64_t award = base * rankFactor;
    const int delta = request.playerLevel - result.monsterLevel;
    // Original single-player level difference factors from D2MOO SUNITDMG_ComputeExperienceGain.
    constexpr std::array lower{256, 256, 256, 256, 256, 256, 207, 159, 110, 61, 13};
    constexpr std::array higher{256, 256, 256, 256, 256, 256, 225, 174, 92, 38, 5};
    if (delta >= 0)
        award = award * lower[std::min(delta, 10)] / 256;
    else if (request.playerLevel < 25)
        award = award * higher[std::min(-delta, 10)] / 256;
    else
        award = award * request.playerLevel / result.monsterLevel;

    std::optional<int> ratio, shift;
    for (size_t index = 0; index < experience.rows().size(); ++index) {
        if (experience.value(index, "Level") == "MaxLvl")
            shift = experience.number(index, "ExpRatio");
        else if (experience.number(index, "Level") == request.playerLevel)
            ratio = experience.number(index, "ExpRatio");
    }
    if (!shift || *shift < 0 || *shift > 30)
        return defer("Invalid original experience ratio scale");
    if (!ratio)
        return defer("Missing original player experience ratio");
    if (*ratio < 0)
        return defer("Invalid original player experience ratio");
    result.amount = award * uint64_t(*ratio) >> *shift;
    return result;
}
} // namespace d2x
