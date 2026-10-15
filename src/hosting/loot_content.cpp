#include "loot_content.hpp"
#include "item_content.hpp"
#include "character_content.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_loot.hpp"
#include "content/world/world_catalog.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/object_loot.hpp"
#include "core/random.hpp"
#include "persistence/d2s_quests.hpp"
#include "persistence/d2s_fixed_sections.hpp"
#include <stdexcept>
namespace d2x {
static void prepareOneLoot(GameHost &host, GameHandle game, Archives &archives, const ClassicData &data, LootContent &cache) {
    // One source per cycle bounds content work. Rotate stalled sources so a
    // crowded landing does not starve unrelated drops; each retains its seed.
    const auto pending = host.pendingLoot(game);
    if (pending.empty()) {cache.cursors.erase(game);return;}
    auto selected=pending.upper_bound(cache.cursors[game]);if(selected==pending.end()) selected=pending.begin();
    auto first=pending.end();
    for(auto entry=pending.begin();entry!=pending.end();++entry)
        if(!entry->second.prepared && (first==pending.end() || entry->second.order<first->second.order)) first=entry;
    if(first!=pending.end()) selected=first;
    const auto source=selected->first;cache.cursors[game]=source;
    const auto preparation=host.beginLootPreparation(game,source);
    if(!preparation) return;
    const auto &request=*preparation;
    if(request.prepared) {host.installLoot(game,source,{},{});return;}
    size_t seedIndex=0;
    const ItemSeedSource seeds=[&] {
        const auto value=host.reserveLootSeeds(game,source,request.request.occurrence,seedIndex);
        if(!value) throw std::runtime_error("Loot item seed reservation is unavailable");
        ++seedIndex;
        return *value;
    };
    if (!cache.monsters) cache.monsters = std::make_shared<const MonsterCatalog>(archives, data.tables.at("monstats"));
    auto &world = cache.worlds[request.request.source.difficulty];
    if (!world) world = std::make_shared<const WorldCatalog>(archives, request.request.source.difficulty);
    LootPlan plan;
    plan.randomState=request.seed;
    std::optional<uint64_t> objectRandom;
    if(request.request.towerGold) {
        const auto level=world->levels().find(int(request.request.source.region));
        if(level==world->levels().end() || level->second.population.level.at(size_t(request.request.source.difficulty))<=0) plan.deferred="Missing original tower gold area level";
        else {
            const auto ilvl=unsigned(level->second.population.level.at(size_t(request.request.source.difficulty)));
            ItemGeneration generation;generation.seeds=seeds();
            auto random=initialRandom(generation.seeds->unit);
            const auto quantity=ilvl+limitedRandom(random,5*ilvl);
            generation.unitRandom=random;
            plan.drops.push_back({"gld",quantity,{},ilvl,std::move(generation)});plan.randomState=request.seed;
        }
    } else if (request.request.object) {
        const auto &object = *request.request.object;
        auto seed = request.seed;
        if(!object.dropCode.empty()) {
            for(int i=0;i<object.dropCount;++i) plan.drops.push_back({object.dropCode,1,{},unsigned(object.dropLevel),{}});
        } else if(object.operation==26) {
            const bool scroll=limitedRandom(seed,20)<=12,identify=limitedRandom(seed,2)!=0;
            plan.drops.push_back({scroll?(identify?"isc":"tsc"):(identify?"ibk":"tbk"),1,{},unsigned(world->level(int(request.request.source.region)).population.level[size_t(request.request.source.difficulty)]),{}});
            plan.randomState=seed;
        } else if(object.operation==19 || object.operation==20) {
            plan=planRackLoot(data,*world,request.request.source.region,request.request.source.difficulty,object.operation==20,seed,request.uniques,request.classCode,seeds);
        } else {
        const auto entry = resolveObjectTreasure(data, *world, request.request.source.region, request.request.source.difficulty);
        if (!entry.deferred.empty()) plan.deferred = entry.deferred;
        else if (object.chest)
            plan = planChestLoot(data, entry, *object.chest, object.definition, seed, request.uniques, request.classCode, request.magicFind, request.goldFind, request.request.source.difficulty, request.effectivePlayers,seeds);
        else {
            const bool drops = object.operation == 1 || object.operation == 14 || object.operation==48 || object.operation==51 || object.operation==68 ||
                ((object.operation == 3 || object.operation == 5) && limitedRandom(seed,100) <= 20);
            if (drops) plan = planItemLoot(data, data.tables.at("itemratio"), entry.treasureClass, entry.itemLevel, 0, seed, request.uniques, request.classCode, request.magicFind, request.goldFind,{},request.effectivePlayers,false,false,seeds);
            else plan.randomState=seed;
        }
        }
        objectRandom=object.chest?seed:plan.randomState;
    } else {
        auto sourceRequest=request.request.source;
        const auto &monstats=data.tables.at("monstats");
        for(size_t row=0;row<monstats.rows().size();++row) if(monstats.value(row,"Id")==sourceRequest.identity.monster) {
            const int slot=monstats.number(row,"TCQuestId").value_or(0),bit=monstats.number(row,"TCQuestCP").value_or(0);
            if(!request.request.environment && slot>0 && slot<48 && bit>=0 && bit<16) {
                D2sFixedSections sections;const auto &record=request.character.player;
                if(!record.nativeSaveSections.empty()) sections=readD2sFixedSections(std::span(reinterpret_cast<const uint8_t *>(record.nativeSaveSections.data()),record.nativeSaveSections.size()));
                exportD2sQuests(record,sections,data.npcDialogues);
                const auto at=10+size_t(sourceRequest.difficulty)*96+size_t(slot)*2;
                const unsigned flags=sections.quests.at(at)|(unsigned(sections.quests.at(at+1))<<8);
                // MonsterMode: killer's COMPLETEDBEFORE, REWARDPENDING and TCQuestCP.
                sourceRequest.questFirstKill=!(flags&((1u<<15)|(1u<<1)|(1u<<bit)));
            }
            break;
        }
        const auto entry = resolveMonsterLoot(data, *cache.monsters, *world, sourceRequest);
        if (entry.status == LootEntryStatus::Ready)
            plan = planItemLoot(data, data.tables.at("itemratio"), entry.treasureClass, entry.itemLevel,
                entry.upgradeLevel, request.seed, request.uniques, request.classCode, request.magicFind, request.goldFind,{},request.effectivePlayers,sourceRequest.ignoreNoDrop,
                sourceRequest.identity.monster=="hellbovine",seeds);
        else if (entry.status == LootEntryStatus::Deferred) plan.deferred = entry.reason;
    }
    auto character = request.character;
    // Temporary generation IDs must not alias admitted inventory IDs. Only
    // this batch is projected into its immutable equipment rules.
    character.inventory.items.clear();
    auto random = plan.randomState;
    server::items::PreparedBatch batch;
    uint64_t id = 1;
    for (auto &drop : plan.drops) {
        if(!drop.generation.seeds) drop.generation.seeds=seeds();
        auto item = prepareItem(data, drop, random, request.request.source.difficulty);
        if (item.quality == ItemQuality::Unique && item.specialRow >= 0 && !data.tables.at("uniqueitems").number(size_t(item.specialRow), "nolimit").value_or(0)) batch.limitedUniques.insert(size_t(item.specialRow));
        item.id = EntityId{id++}; item.location = ContainerLocation{character.containers.backpack, {}};
        character.inventory.items.emplace(item.id, item); batch.items.push_back(std::move(item));
    }
    batch.equipment = prepareEquipmentRules(data, character);
    host.installLoot(game, source, std::move(batch), std::move(plan.deferred),plan.randomState,objectRandom);
}
}

namespace d2x {
void preparePendingLoot(GameHost &host, GameHandle game, Archives &archives, const ClassicData &data, LootContent &cache) {
    try { prepareOneLoot(host, game, archives, data, cache); }
    catch (const std::exception &error) {
        const auto pending = host.pendingLoot(game); if (pending.empty()) return;
        const auto source=pending.find(cache.cursors[game]);if(source==pending.end()) return;
        server::items::PreparedBatch unavailable; unavailable.equipment = std::make_shared<const server::EquipmentRules>();
        host.installLoot(game, source->first, std::move(unavailable), error.what());
    }
}
}
