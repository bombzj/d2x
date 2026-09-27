#include "monster_loot.hpp"
#include <algorithm>

namespace d2x {
MonsterLootEntry resolveMonsterLoot(const ClassicData &data, const MonsterCatalog &monsters,
                                  const WorldCatalog &world, const LootRequest &request) {
    MonsterLootEntry result;
    auto defer = [&](std::string reason) {
        result.reason = std::move(reason);
        return result;
    };
    if (data.profile != "lod-named-txt-v1")
        return defer("Unsupported monster loot data profile");
    if (request.difficulty < 0 || request.difficulty > 2)
        return defer("Invalid loot difficulty");
    const auto *monster = monsters.find(request.identity.monster);
    if (!monster || !monster->hostile())
        return defer("Unknown, ambiguous or non-hostile monster identity");
    const auto &table = data.tables.at("monstats");
    auto source = std::find_if(data.monsters.begin(), data.monsters.end(),
                               [&](const auto &record) { return record.name == monster->id; });
    if (source == data.monsters.end())
        return defer("Missing monster treasure record");
    size_t row = 0;
    for (; row < table.rows().size(); ++row)
        if (table.value(row, "Id") == monster->id)
            break;
    if (row == table.rows().size())
        return defer("Missing original monster row");
    const char *suffix[] = {"", "(N)", "(H)"};
    auto level = table.number(row, std::string("Level") + suffix[request.difficulty]);
    const bool scales = request.difficulty > 0 && !table.number(row, "noRatio").value_or(0) &&
                        !table.number(row, "boss").value_or(0);
    if (scales) {
        auto area = world.levels().find(int(request.region));
        if (area == world.levels().end() || !area->second.population.supported)
            return defer("Missing area monster level");
        level = area->second.population.level[request.difficulty];
    }
    if (!level || *level < 1 || *level > 99)
        return defer("Unsupported monster level");
    int slot = 0;
    int bonus = 0;
    const SuperUniqueRecord *superUnique = nullptr;
    switch (request.identity.rank) {
    case MonsterRank::Normal:
    case MonsterRank::Boss:
        break;
    case MonsterRank::Champion:
        slot = 1;
        bonus = 2;
        break;
    case MonsterRank::Unique:
        slot = 2;
        bonus = 3;
        break;
    case MonsterRank::Minion:
        return defer("Minion level bonus requires its owning group modifiers");
    case MonsterRank::SuperUnique:
        superUnique = monsters.superUnique(request.identity.superUnique);
        if (!superUnique || superUnique->monster != monster->id)
            return defer("Missing or mismatched super unique identity");
        for (int modifier : superUnique->modifiers)
            if (modifier == 4 || modifier == 16 || (modifier >= 36 && modifier <= 39))
                return defer("Super unique has additional level-affecting modifiers");
        bonus = 3;
        break;
    default:
        return defer("Invalid monster rank");
    }
    if (!superUnique && !request.identity.superUnique.empty())
        return defer("Super unique identity conflicts with monster rank");
    if (request.identity.enchantment) bonus = request.identity.enchantment->levelBonus;
    result.itemLevel = *level + bonus;
    if (result.itemLevel > 99)
        return defer("Monster item level exceeds supported instance range");
    result.upgradeLevel = scales ? result.itemLevel : 0;
    const bool questClass = table.number(row, "TCQuestId").value_or(0) &&
                            source->treasureClasses[request.difficulty][3];
    if (questClass && request.questFirstKill)
        slot = 3;
    auto entry = source->treasureClasses[request.difficulty][slot];
    if (superUnique)
        result.treasureClass = superUnique->treasureClasses[request.difficulty];
    else if (entry)
        result.treasureClass = data.treasures.at(*entry).name;
    if (result.treasureClass.empty()) {
        result.status = LootEntryStatus::Empty;
        result.reason = "Original monster treasure slot is empty";
        return result;
    }
    if (std::none_of(data.treasures.begin(), data.treasures.end(),
                     [&](const auto &record) { return record.name == result.treasureClass; }))
        return defer("Unknown monster treasure class");
    result.status = LootEntryStatus::Ready;
    return result;
}
} // namespace d2x
