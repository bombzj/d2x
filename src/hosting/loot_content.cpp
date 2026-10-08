#include "loot_content.hpp"
#include "item_content.hpp"
#include "character_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_loot.hpp"
#include "content/world/world_catalog.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/object_loot.hpp"
#include "core/random.hpp"
namespace d2x {
static void prepareOneLoot(GameHost &host, GameHandle game, Archives &archives, const ClassicData &data, LootContent &cache) {
    // One source per scheduler cycle bounds content work and preserves unique
    // availability between deaths. A failed handoff retains the same seed.
    const auto pending = host.pendingLoot(game);
    if (pending.empty()) return;
    const auto &[source, request] = *pending.begin();
    if (!cache.monsters) cache.monsters = std::make_shared<const MonsterCatalog>(archives, data.tables.at("monstats"));
    auto &world = cache.worlds[request.request.source.difficulty];
    if (!world) world = std::make_shared<const WorldCatalog>(archives, request.request.source.difficulty);
    LootPlan plan;
    if (request.request.object) {
        const auto &object = *request.request.object;
        auto seed = request.seed;
        if(object.operation==19 || object.operation==20) {
            plan=planRackLoot(data,*world,request.request.source.region,request.request.source.difficulty,object.operation==20,seed,request.uniques,request.classCode);
        } else {
        const auto entry = resolveObjectTreasure(data, *world, request.request.source.region, request.request.source.difficulty);
        if (!entry.deferred.empty()) plan.deferred = entry.deferred;
        else if (object.chest)
            plan = planChestLoot(data, entry, *object.chest, object.definition, seed, request.uniques, request.classCode, request.magicFind, request.goldFind, request.request.source.difficulty, request.effectivePlayers);
        else {
            const bool drops = object.operation == 1 || object.operation == 14 ||
                ((object.operation == 3 || object.operation == 5) && limitedRandom(seed,100) <= 20);
            if (drops) plan = planItemLoot(data, data.tables.at("itemratio"), entry.treasureClass, entry.itemLevel, 0, seed, request.uniques, request.classCode, request.magicFind, request.goldFind,{},request.effectivePlayers);
        }
        }
    } else {
        const auto entry = resolveMonsterLoot(data, *cache.monsters, *world, request.request.source);
        if (entry.status == LootEntryStatus::Ready)
            plan = planItemLoot(data, data.tables.at("itemratio"), entry.treasureClass, entry.itemLevel,
                entry.upgradeLevel, request.seed, request.uniques, request.classCode, request.magicFind, request.goldFind,{},request.effectivePlayers);
        else if (entry.status == LootEntryStatus::Deferred) plan.deferred = entry.reason;
    }
    auto character = request.character;
    auto random = plan.randomState ? plan.randomState : request.seed;
    server::items::PreparedBatch batch;
    uint64_t id = 1;
    for (const auto &drop : plan.drops) {
        auto item = prepareItem(data, drop, random, request.request.source.difficulty);
        if (item.quality == ItemQuality::Unique && item.specialRow >= 0 && !data.tables.at("uniqueitems").number(size_t(item.specialRow), "nolimit").value_or(0)) batch.limitedUniques.insert(size_t(item.specialRow));
        item.id = EntityId{id++}; item.location = ContainerLocation{character.containers.backpack, {}};
        character.inventory.items.emplace(item.id, item); batch.items.push_back(std::move(item));
    }
    batch.equipment = prepareEquipmentRules(data, character);
    host.installLoot(game, source, std::move(batch), std::move(plan.deferred));
}
}

namespace d2x {
void preparePendingLoot(GameHost &host, GameHandle game, Archives &archives, const ClassicData &data, LootContent &cache) {
    try { prepareOneLoot(host, game, archives, data, cache); }
    catch (const std::exception &error) {
        const auto pending = host.pendingLoot(game); if (pending.empty()) return;
        server::items::PreparedBatch unavailable; unavailable.equipment = std::make_shared<const server::EquipmentRules>();
        host.installLoot(game, pending.begin()->first, std::move(unavailable), error.what());
    }
}
}
