#include "object_loot.hpp"
#include <algorithm>

namespace d2x {
ObjectTreasureEntry resolveAct1ObjectTreasure(const ClassicData &data, const WorldCatalog &world,
                                               RegionId region, int difficulty) {
    ObjectTreasureEntry result;
    const auto &levels = world.levels();
    const auto area = levels.find(int(region));
    const auto first = levels.find(2);
    const auto last = levels.find(37);
    if (difficulty < 0 || difficulty > 2 || area == levels.end() || first == levels.end() ||
        last == levels.end() || area->second.act != first->second.act ||
        !area->second.population.supported) {
        result.deferred = "Object treasure requires a supported Act I area and difficulty";
        return result;
    }
    const int level = area->second.population.level[size_t(difficulty)];
    const int minimum = first->second.population.level[size_t(difficulty)];
    const int maximum = last->second.population.level[size_t(difficulty)];
    if (level < 1 || level > 99 || minimum < 1 || maximum < minimum) {
        result.deferred = "MPQ area levels do not resolve an Act I chest tier";
        return result;
    }
    const int offset = (maximum - minimum + 1) / 3;
    int tier = level >= minimum + offset ? 1 : 0;
    if (level >= minimum + 2 * offset) tier = 2;
    const std::string name = "Act 1 Chest " + std::string(1, char('A' + tier));
    if (std::none_of(data.treasures.begin(), data.treasures.end(),
                     [&](const auto &record) { return record.name == name; })) {
        result.deferred = "MPQ TreasureClassEx lacks " + name;
        return result;
    }
    result.treasureClass = name;
    result.itemLevel = level;
    return result;
}
LootPlan planAct1RackLoot(const ClassicData &data, const WorldCatalog &world, RegionId region,
                         int difficulty, bool weapon, uint64_t seed) {
    LootPlan plan;
    plan.randomState = seed;
    auto area = world.levels().find(int(region));
    if (area == world.levels().end() || area->second.act != 0 || difficulty < 0 || difficulty > 2) {
        plan.deferred = "Rack requires an MPQ Act I area level";
        return plan;
    }
    int level = std::max(1, area->second.population.level[size_t(difficulty)] - 1);
    auto random = [&](uint32_t bound) {
        plan.randomState = uint64_t(uint32_t(plan.randomState)) * 0x6ac690c5ULL +
                           (plan.randomState >> 32);
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
        plan.drops.push_back({chosen->code, 1, {2, 3}, unsigned(level), {}});
        break;
    }
    return plan;
}
} // namespace d2x
