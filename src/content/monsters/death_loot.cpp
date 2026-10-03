#include "death_loot.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_quality.hpp"
#include "core/random.hpp"
#include <stdexcept>

namespace d2x {
PreparedMonsterDeathLoot prepareMonsterDeathLoot(const ClassicData &data,
    const MonsterCatalog &monsters, const WorldCatalog &world, const LootRequest &request,
    const EnemyDied &death, DeathLootContext context, const std::set<uint32_t> &usedUniques,
    uint64_t &questRandom) {
    PreparedMonsterDeathLoot result;
    result.entry = resolveMonsterLoot(data, monsters, world, request);
    if (death.noTreasure) {
        result.entry.status = LootEntryStatus::Empty;
        result.entry.reason = "Original monster NOTC flag";
    }
    auto &plan = result.plan;
    const auto &entry = result.entry;
    plan.randomState = death.lootRandom;
    if (entry.status == LootEntryStatus::Ready) {
        auto ratios = data.tables.find("itemratio");
        if (ratios == data.tables.end()) plan.deferred = "Missing original ItemRatio table";
        else {
            std::set<size_t> uniqueRows;
            for (auto row : usedUniques) uniqueRows.insert(size_t(row));
            plan = planItemLoot(data, ratios->second, entry.treasureClass, entry.itemLevel,
                entry.upgradeLevel, plan.randomState, uniqueRows, context.characterClass,
                death.magicFind, death.goldFind);
        }
    } else if (entry.status == LootEntryStatus::Deferred) plan.deferred = entry.reason;
    result.treasureDrops = plan.drops.size();
    if (context.andarielBonus && plan.deferred.empty()) {
        // Existing A1Q6 reward codes/order, moved unchanged from session_loot.
        // The quest stream is distinct from the dead unit's TC stream.
        constexpr const char *chipped[]{"gcv", "gcr", "gcb", "gcy", "gcg", "gcw", "skc"};
        constexpr const char *normal[]{"gsv", "gsr", "gsb", "gsy", "gsg", "gsw", "sku"};
        for (int gem = 0; gem < 3; ++gem) {
            const auto code = gem < 2 ? chipped[limitedRandom(questRandom, 7)]
                                     : normal[limitedRandom(questRandom, 7)];
            if (!data.items.find(code)) throw std::runtime_error("Missing Andariel quest gem");
            plan.drops.push_back({code, 1, {}, unsigned(entry.itemLevel), {}});
        }
    }
    return result;
}
} // namespace d2x
