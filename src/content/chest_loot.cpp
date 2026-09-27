#include "core/random.hpp"
#include "object_loot.hpp"
#include "item_quality.hpp"
#include <iterator>

namespace d2x {
namespace {
uint32_t roll(uint64_t &seed, uint32_t bound) {
    return limitedRandom(seed, bound);
}
bool magical(ItemQuality quality) {
    return quality == ItemQuality::Magic || quality == ItemQuality::Rare ||
           quality == ItemQuality::Set || quality == ItemQuality::Unique;
}
}
LootPlan planChestLoot(const ClassicData &data, const ObjectTreasureEntry &entry,
                      const ChestState &chest, int objectClass, uint64_t &objectSeed,
                      const std::set<size_t> &usedUniques, std::string_view characterClass,
                      int magicFind, int goldFind) {
    LootPlan plan;
    plan.randomState = chest.lootSeed;
    plan.deferred = entry.deferred;
    if (!plan.deferred.empty()) return plan;
    const auto ratios = data.tables.find("itemratio");
    if (ratios == data.tables.end()) {
        plan.deferred = "MPQ ItemRatio is unavailable";
        return plan;
    }
    auto uniques = usedUniques;
    // One native DropTC call is capped at six items; a locked chest makes TWO
    // independent calls. Never cap their combined output at six or duplicate one roll.
    auto drop = [&](std::optional<DropQuality> quality = std::nullopt) {
        auto batch = planItemLoot(data, ratios->second, entry.treasureClass, entry.itemLevel,
                                 0, plan.randomState, uniques, characterClass, magicFind, goldFind, quality);
        plan.randomState = batch.randomState;
        plan.noDrops += batch.noDrops;
        if (!batch.deferred.empty()) plan.deferred = batch.deferred;
        const int first = batch.drops.empty() ? 0 : magical(batch.drops.front().generation.quality) ? 2 : 1;
        for (const auto &item : batch.drops)
            if (item.generation.quality == ItemQuality::Unique && item.generation.specialRow >= 0)
                uniques.insert(size_t(item.generation.specialRow));
        plan.drops.insert(plan.drops.end(), std::make_move_iterator(batch.drops.begin()),
                          std::make_move_iterator(batch.drops.end()));
        return first; // ObjMode examines the first item, not every item in the batch.
    };
    auto direct = [&](const char *code, int count) {
        const auto *item = data.items.find(code);
        if (!item || !item->artAvailable) {
            plan.deferred = "Original chest drop is unavailable: " + std::string(code);
            return;
        }
        for (int index = 0; index < count; ++index) {
            // OBJMODE_DropItemWithCodeAndQuality has no player MF/GF source.
            // ITEMS_GetItemLevelForNewItem(object, 0) returns 1 for these extras.
            const unsigned quantity = item->equipment.isType("gold")
                ? 1 + roll(plan.randomState, 5) : 1;
            plan.drops.push_back({code, quantity, {2, 3}, 1, {}});
        }
    };
    std::optional<DropQuality> quality;
    if (chest.sparkly) quality = roll(objectSeed, 100) < 5 ? DropQuality::Rare : DropQuality::Magic;
    if (objectClass != 397) {
        if (roll(objectSeed, 100) >= 25 || chest.sparkly || chest.locked) {
            int magicalBatches = 0;
            for (int i = 0; i < (chest.locked ? 2 : 1); ++i)
                if (drop(quality) == 2) ++magicalBatches;
            if (chest.sparkly && !magicalBatches)
                for (int i = 0; i < 10; ++i)
                    if (drop(quality) == 2) break;
        }
        return plan;
    }
    // The sparkly Objects.Id=397 has its own six outcome branches. Locking still
    // requires a key, but does not multiply these native branch counts.
    const auto outcome = roll(objectSeed, 10000);
    int items = 0, magic = 0;
    if (outcome < 1200) {
        quality = outcome < 200 ? DropQuality::Unique : outcome < 600 ? DropQuality::Set : DropQuality::Rare;
        for (int i = 0; i < 2; ++i) {
            const int first = drop(quality);
            if (!first) break;
            if (first == 2) return plan;
        }
    } else if (outcome < 3200) {
        for (int i = 0; i < 10; ++i) {
            const int first = drop(DropQuality::Magic);
            if (first) ++items;
            if (first == 2) ++magic;
            if (magic >= 3) break;
        }
        if (items) return plan;
    } else if (outcome < 6200) {
        for (int i = 0; i < 10; ++i) {
            const int first = drop(DropQuality::Magic);
            if (first == 2) ++magic;
            else if (first) ++items;
            if (magic >= 2) break;
        }
        if (!items && drop()) items = 1;
        direct("gld", 7 - items);
        return plan;
    }
    for (int i = 0; i < 10; ++i) {
        const int first = drop(DropQuality::Magic);
        if (first == 2) break;
        if (first) ++items;
    }
    for (int i = items; i < 4; ++i) drop();
    direct("gld", 5);
    direct("hp3", 2);
    direct("mp3", 2);
    return plan;
}
} // namespace d2x
