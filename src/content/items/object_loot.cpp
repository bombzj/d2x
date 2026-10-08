#include "core/random.hpp"
#include "object_loot.hpp"
#include "item_quality.hpp"
#include <algorithm>
#include <cstdlib>

namespace d2x {
ObjectTreasureEntry resolveObjectTreasure(const ClassicData &data, const WorldCatalog &world,
                                               RegionId region, int difficulty) {
    ObjectTreasureEntry result;
    const auto &levels = world.levels();
    const auto area = levels.find(int(region));
    if (difficulty < 0 || difficulty > 2 || area == levels.end() ||
        area->second.act < 0 || area->second.act >= 5 || !area->second.population.supported) {
        result.deferred = "Object treasure requires an MPQ area and difficulty";
        return result;
    }
    // ObjMode.cpp::OBJMODE_DropFromChestTCWithQuality. Act V starts at town level zero.
    constexpr int bounds[5][2]{{2, 37}, {41, 73}, {76, 102}, {104, 108}, {109, 132}};
    const int act = area->second.act;
    const auto first = levels.find(bounds[act][0]);
    const auto last = levels.find(bounds[act][1]);
    if (first == levels.end() || last == levels.end()) {
        result.deferred = "MPQ Levels lacks the chest tier boundary areas";
        return result;
    }
    const int level = area->second.population.level[size_t(difficulty)];
    const int minimum = first->second.population.level[size_t(difficulty)];
    const int maximum = last->second.population.level[size_t(difficulty)];
    if (level < 1 || level > 99 || minimum < 0 || maximum < 0) {
        result.deferred = "MPQ area levels do not resolve a chest tier";
        return result;
    }
    const int offset = (std::abs(maximum - minimum) + 1) / 3;
    int tier = level >= minimum + offset ? 1 : 0;
    if (level >= minimum + 2 * offset) tier = 2;
    constexpr const char *suffixes[]{"", " (N)", " (H)"};
    const std::string name = "Act " + std::to_string(act + 1) + suffixes[difficulty] +
        " Chest " + char('A' + tier);
    if (std::none_of(data.treasures.begin(), data.treasures.end(),
                     [&](const auto &record) { return record.name == name; })) {
        result.deferred = "MPQ TreasureClassEx lacks " + name;
        return result;
    }
    result.treasureClass = name;
    result.itemLevel = level;
    return result;
}
LootPlan planRackLoot(const ClassicData &data, const WorldCatalog &world, RegionId region,
    int difficulty,bool weapon,uint64_t seed,const std::set<size_t> &used,std::string_view characterClass) {
    LootPlan plan;
    plan.randomState = seed;
    auto area = world.levels().find(int(region));
    if (area == world.levels().end() || difficulty < 0 || difficulty > 2) {
        plan.deferred = "Rack requires an MPQ area level";
        return plan;
    }
    int level = std::max(1, area->second.population.level[size_t(difficulty)] - 1);
    auto random = [&](uint32_t bound) {
        rollRandom(plan.randomState);
        return bound ? uint32_t(plan.randomState) % bound : 0;
    };
    for (int attempt = 0; attempt < (weapon ? 6 : 1); ++attempt) {
        std::vector<const ItemDefinition *> candidates;
        for (const auto &[code, item] : data.items.entries()) {
            if (item.family != (weapon ? ItemFamily::Weapon : ItemFamily::Armor) ||
                !item.artAvailable || !item.base.spawnable.value_or(0) ||
                item.base.level.value_or(100) > level) continue;
            const auto &source = data.tables.at(item.base.sourceTable);
            if (source.number(item.base.sourceRow, "quest").value_or(0)) continue;
            const int rarity = item.base.rarity.value_or(1);
            if (random(uint32_t(std::max(1, rarity))) != 0) continue;
            candidates.push_back(&item);
        }
        if (candidates.empty()) continue;
        const auto *chosen = candidates[size_t(random(uint32_t(candidates.size())))];
        const auto &source = data.tables.at(chosen->base.sourceTable);
        if (weapon && !(source.number(chosen->base.sourceRow, "bitfield1").value_or(0) & 2))
            continue;
        plan=planSelectedItem(data,chosen->code,level,plan.randomState,used,characterClass,{},true);
        break;
    }
    return plan;
}
} // namespace d2x
