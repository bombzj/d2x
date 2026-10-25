#include "loot_content.hpp"
#include "content/monsters/monster_identity.hpp"
#include "item_content.hpp"
#include "character_content.hpp"
#include "combat_content.hpp"
#include "pd2/map_loot.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "content/monsters/monster_catalog.hpp"
#include "content/monsters/monster_loot.hpp"
#include "content/world/world_catalog.hpp"
#include "content/items/item_quality.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/object_loot.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/native_sources.hpp"
#include "core/random.hpp"
#include "persistence/d2s_quests.hpp"
#include "persistence/d2s_fixed_sections.hpp"
#include <algorithm>
#include <array>
#include <set>
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
    if (!cache.monsters) cache.monsters = std::make_shared<const MonsterCatalog>(archives, data.tables.at("monstats"),
        data.pd2Host?MonsterPreparation::TownActors:MonsterPreparation::FullRules);
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
        objectRandom=object.chest && object.definition!=371?seed:plan.randomState;
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
        if(request.pd2Loot && request.pd2Monster && entry.status!=LootEntryStatus::Deferred)
            plan=planPd2MapLoot(data,request,entry,seeds);
        else if (entry.status == LootEntryStatus::Ready)
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
    auto programRandom=request.pd2Random;
    for (auto &drop : plan.drops) {
        if(!drop.generation.seeds) drop.generation.seeds=seeds();
        if(request.pd2Map) preparePd2MapItemDrop(request.pd2Map->properties,drop,programRandom);
        uint64_t itemRandom{};
        auto item = prepareItem(data, drop, random, request.request.source.difficulty,0,&itemRandom);
        if(request.pd2Map) finishPd2MapItemDrop(data,request.pd2Map->properties,item,itemRandom,programRandom);
        if (item.quality == ItemQuality::Unique && item.specialRow >= 0 && !data.tables.at("uniqueitems").number(size_t(item.specialRow), "nolimit").value_or(0)) batch.limitedUniques.insert(size_t(item.specialRow));
        item.id = EntityId{id++}; item.location = ContainerLocation{character.containers.backpack, {}};
        character.inventory.items.emplace(item.id, item); batch.items.push_back(std::move(item));
    }
    batch.equipment = prepareEquipmentRules(data, character);
    host.installLoot(game, source, std::move(batch), std::move(plan.deferred),plan.randomState,objectRandom,
        programRandom!=request.pd2Random?std::optional{programRandom}:std::nullopt);
}
}

namespace d2x {
void preparePendingLoot(GameHost &host, GameHandle game, Archives &archives, const ClassicData &data, LootContent &cache) {
    if(const auto request=host.pendingMonsterEquipment(game)) {
        server::monsters::PreparedEquipment prepared;prepared.token=request->token;
        prepared.gameRandom=request->gameRandom;prepared.unitRandom=request->unitRandom;
        try {
            auto equipment=std::make_shared<PersistentCharacter>();equipment->player.id=request->actor;
            equipment->player.level=request->rule.level;equipment->containers.equipment=request->actor;
            equipment->inventory.containers.emplace(request->actor,ContainerState{request->actor,
                {request->actor,ContainerKind::Equipment,int(EquipmentSlot::Count),1}});
            EquipmentLoadout loadout;loadout.skipRequirements=true;
            loadout.requirementPercent=[](const ItemInstance &){return 0;};
            loadout.maximumDurability=[&](const ItemInstance &item){return itemMaximumDurability(data,item,resolveItemStats(data,item,request->rule.level));};
            std::map<EquipmentSlot,ItemDefinition> definitions;
            std::set<EntityId> unidentified;
            auto uniques=request->uniques;
            const auto selection=server::monsters::selectBirthEquipment(request->rule,prepared.unitRandom);
            if(!selection) throw std::runtime_error("Invalid native MonEquip choices");
            constexpr std::array qualities{DropQuality::Inferior,DropQuality::Inferior,DropQuality::Normal,
                DropQuality::Superior,DropQuality::Magic,DropQuality::Set,DropQuality::Rare,DropQuality::Unique};
            for(const auto &choice:*selection.value) {
                const auto slot=EquipmentSlot(choice.body-1);if(definitions.contains(slot)) continue;
                const auto *definition=data.items.find(choice.code);if(!definition) continue;
                if(prepared.allocatedItems>=UINT32_MAX-request->itemCursor) throw std::runtime_error("Native MonEquip identity capacity exceeded");
                const EntityId id{request->itemCursor+prepared.allocatedItems++};
                auto item=prepareNativeItem(data,choice.code,request->rule.level,request->rule.difficulty,prepared.gameRandom,
                    choice.quality?std::optional{qualities.at(choice.quality)}:std::nullopt,uniques,NativeItemUse::Equipment);
                if(item.quality==ItemQuality::Unique && item.specialRow>=0 &&
                    !data.tables.at("uniqueitems").number(size_t(item.specialRow),"nolimit").value_or(0)) {
                    uniques.insert(size_t(item.specialRow));prepared.limitedUniques.insert(size_t(item.specialRow));
                }
                item.id=id;item.location=ContainerLocation{request->actor,{int(slot),0}};
                if(!item.identified) {unidentified.insert(id);item.identified=true;}
                auto &stored=equipment->inventory.items.emplace(id,std::move(item)).first->second;
                auto &bound=definitions.emplace(slot,*definition).first->second;
                loadout.equipped.at(size_t(slot))={&stored,&bound};
                equipment->inventory.itemOrder.push_back(id);
                loadout.inventory.push_back({&stored,&bound});
            }
            const EquipmentActor actor{"",request->strength,request->dexterity,request->rule.level,0,0,false,request->rule.nativeClass};
            std::vector<EquipmentContribution> contributions;
            const EquipmentContributionSource instructions{{},[&](const ItemInstance &item,int level) {
                auto identified=item;identified.identified=true;return resolveItemStats(data,identified,level);
            },{},{},{},[&](EquipmentContribution contribution){contributions.push_back(std::move(contribution));}};
            const auto modifiers=deriveEquipmentModifiers(loadout,actor,instructions);
            const EquipmentActor improved{"",actor.strength+modifiers.strength,actor.dexterity+modifiers.dexterity,actor.level,0,0,false,actor.nativeClass};
            auto gear=deriveEquipmentStats(loadout,improved,request->defense+modifiers.defense,modifiers.combat,request->rating+modifiers.attackRating);
            const auto &types=data.tables.at("itemtypes");
            for(auto &weapon:gear.weapons) if(weapon.item) {
                const auto &definition=*data.items.find(equipment->inventory.items.at(weapon.item).definition);
                weapon.strengthBonus=definition.base.strengthBonus.value_or(0);weapon.dexterityBonus=definition.base.dexterityBonus.value_or(0);
                for(size_t row=0;row<types.rows().size();++row)
                    if(!types.value(row,"Code").empty() && definition.equipment.isType(types.value(row,"Code"))) weapon.typeLayers.push_back(uint16_t(row));
            }
            equipment->inventory.containers.at(request->actor).registeredWeapon=gear.weapons[0].item;
            std::stable_sort(contributions.begin(),contributions.end(),[](const auto &first,const auto &second){return first.item<second.item;});
            prepared.sources=prepareNativeEquipmentSources(data.items,*request->rule.stats,contributions,request->actor,
                [&](EntityId id)->const ItemInstance &{return equipment->inventory.items.at(id);},gear.weapons[0].item,StatOwnerKind::Monster).contributions;
            if(gear.weapons[0].item) prepared.weapon=gear.weapons[0];
            for(const auto id:unidentified) equipment->inventory.items.at(id).identified=false;
            prepared.stats=gear;prepared.equipment=std::move(equipment);
        } catch(const std::exception &error) {prepared.deferred=error.what();}
        const auto installed=host.prepareMonsterEquipment(game,std::move(prepared));
        if(installed.status==server::DomainStatus::InvalidRequest) {
            server::monsters::PreparedEquipment failure;failure.token=request->token;failure.deferred="Native MonEquip failed authority validation";
            host.prepareMonsterEquipment(game,std::move(failure));
        }
        return;
    }
    if(const auto request=host.pendingModifiers(game)) {
        server::monsters::PreparedModifiers prepared;prepared.token=request->token;
        try {
            const auto settings=host.settings(game);const auto area=host.area(game,request->area);
            if(!settings || !area || area->generation!=request->generation) throw std::runtime_error("Native modifier area expired");
            const AreaGenerationRequest generation{settings->mapSeed,area->definition.act,int(request->area),settings->difficulty,settings->ladderMonsterStats(),area->definition.pd2Map};
            const auto actualIdentity=[&](const server::monsters::ModifierPreparation::Unit &unit) {
                auto identity=unit.identity;const auto &stats=data.tables.at("monstats");
                identity.monster=std::string(stats.value(compiledMonsterRow(stats,unit.nativeClass),"Id"));return identity;
            };
            const auto leader=prepareSelectedMonster(archives,data,generation,actualIdentity(request->leader),
                request->leader.level,request->selection);
            if(!leader || !leader->rule.enchantment) throw std::runtime_error("Native modifier leader rules unavailable");
            prepared.leader=leader->rule;
            for(const auto &unit:request->minions) {
                auto identity=actualIdentity(unit);identity.rank=MonsterRank::Minion;
                const auto minion=prepareSelectedMonster(archives,data,generation,std::move(identity),unit.level,
                    request->selection,&*leader->rule.enchantment);
                if(!minion) throw std::runtime_error("Native modifier minion rules unavailable");
                auto rule=minion->rule;
                if(rule.enchantment) rule.enchantment->ids=unit.modifiers;
                if(request->promoteMinions && rule.enchantment) {
                    const auto &mods=data.tables.at("monumod");
                    size_t inspected=unit.modifiers.size();
                    for(const int modifier:request->selection.ids) {
                        if(inspected++>=9) break;
                        for(size_t row=0;row<mods.rows().size();++row)
                            if(mods.number(row,"id")==modifier && mods.number(row,"xfer").value_or(0)) {
                                rule.enchantment->ids.push_back(modifier);break;
                            }
                    }
                }
                prepared.minions.emplace(unit.id,std::move(rule));
            }
        } catch(const std::exception &error) {prepared.deferred=error.what();}
        const auto installed=host.prepareModifiers(game,std::move(prepared));
        if(installed.status==server::DomainStatus::InvalidRequest) {
            server::monsters::PreparedModifiers failure;failure.token=request->token;
            failure.deferred="Native modifier rules failed authority validation";
            host.prepareModifiers(game,std::move(failure));
        }
        return;
    }
    if(const auto request=host.pendingGroundGeneration(game)) {
        try {
            auto plan=planCreatedItem(data,request->code,request->level,request->uniques,request->seeds);
            if(!plan.deferred.empty() || plan.drops.size()!=1) {
                host.prepareGroundGeneration(game,request->request,{},plan.deferred.empty()?"Native ground generation produced no item":plan.deferred);
                return;
            }
            uint64_t unused{};
            auto item=prepareItem(data,plan.drops.front(),unused,request->difficulty);
            item.id=EntityId{1};item.nativeFlags|=0x82010;
            PersistentCharacter context;context.inventory.items.emplace(item.id,item);
            server::items::PreparedBatch batch;batch.equipment=prepareEquipmentRules(data,context);
            if(item.quality==ItemQuality::Unique && item.specialRow>=0 &&
                !data.tables.at("uniqueitems").number(size_t(item.specialRow),"nolimit").value_or(0))
                batch.limitedUniques.insert(size_t(item.specialRow));
            batch.items.push_back(std::move(item));
            host.prepareGroundGeneration(game,request->request,std::move(batch),{});
        } catch(const std::exception &error) {host.prepareGroundGeneration(game,request->request,{},error.what());}
        return;
    }
    try { prepareOneLoot(host, game, archives, data, cache); }
    catch (const std::exception &error) {
        const auto pending = host.pendingLoot(game); if (pending.empty()) return;
        const auto source=pending.find(cache.cursors[game]);if(source==pending.end()) return;
        server::items::PreparedBatch unavailable; unavailable.equipment = std::make_shared<const server::EquipmentRules>();
        host.installLoot(game, source->first, std::move(unavailable), error.what());
    }
}
}
