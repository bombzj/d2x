#include "map_loot.hpp"
#include "server/systems/loot/system.hpp"
#include "content/classic_data.hpp"
#include "content/monsters/monster_loot.hpp"
#include "content/items/item_quality.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "content/pd2/cube.hpp"
#include <algorithm>

namespace d2x {
void preparePd2MapItemDrop(const pd2::MapProperties &properties,LootDrop &drop,uint32_t &programRandom) {
    const auto chance=pd2::mapGlobalValue(properties,13);
    if(chance>0 && int(pd2CrtRandom(programRandom)%100)<chance) drop.generation.etherealDropFlag=true;
}
void finishPd2MapItemDrop(const ClassicData &data,const pd2::MapProperties &properties,ItemInstance &item,uint64_t &itemRandom,uint32_t &programRandom) {
    const auto chance=pd2::mapGlobalValue(properties,6);
    if(chance>0 && int(pd2CrtRandom(programRandom)%100)<chance)
        applyPd2AutomaticCorruption(data,item,itemRandom,programRandom);
    const auto sockets=pd2::mapGlobalValue(properties,11);
    const auto *base=data.items.find(item.definition);
    if(sockets<=0 || !base || item.sockets || !item.socketedItems.empty() || (item.nativeFlags&0x1900u) ||
       (base->questTag && item.definition!="leg")) return;
    const int maximum=std::max(0,std::min({base->base.sockets.value_or(0),
        base->base.socketsByLevel[item.level>40?2:item.level>25?1:0],base->width*base->height,6}));
    if(!maximum || int(pd2CrtRandom(programRandom)%100)>=sockets) return;
    // 0x363500 checks the actual primary ItemTypes identity, after the chance
    // draw. 0x37F700 then rolls 1..2 or 2..4 for the base's n2Handed flag.
    const auto &types=data.tables.at("itemtypes");
    for(const size_t id:{size_t(38),size_t(48),size_t(56)})
        if(base->base.type==types.value(id,"Code")) return;
    const auto low=base->equipment.twoHanded?2u:1u,high=base->equipment.twoHanded?4u:2u;
    item.sockets=std::min(unsigned(maximum),low+pd2UnitEventRandom(itemRandom)%(high-low+1));
    item.nativeFlags|=0x800u;
}
LootPlan planPd2MapLoot(const ClassicData &data,const server::loot::Preparation &request,const MonsterLootEntry &entry,const ItemSeedSource &seeds) {
    LootPlan result;result.randomState=request.seed;
    auto uniques=request.uniques;
    const auto append=[&](LootPlan stage,uint64_t &random) {
        if(!stage.deferred.empty()) {result.deferred=std::move(stage.deferred);return false;}
        random=stage.randomState;result.noDrops+=stage.noDrops;
        for(auto &drop:stage.drops) {
            if(drop.generation.quality==ItemQuality::Unique && drop.generation.specialRow>=0 &&
               !data.tables.at("uniqueitems").number(size_t(drop.generation.specialRow),"nolimit").value_or(0))
                uniques.insert(size_t(drop.generation.specialRow));
            result.drops.push_back(std::move(drop));
        }
        return true;
    };
    const auto special=[&](std::string_view code,int uniqueRow,uint64_t &random) {
        std::string_view token=code;
        if(uniqueRow>=0) {
            // Native 0x37F550 clears this exact game's unique bit before a
            // forced map creation; rarity0 does not disable this source.
            uniques.erase(size_t(uniqueRow));
            const auto definition=std::find_if(data.uniqueItems.begin(),data.uniqueItems.end(),
                [&](const auto &item){return item.row==size_t(uniqueRow) && item.code==code;});
            if(definition==data.uniqueItems.end()) {result.deferred="Missing native forced unique map";return false;}
            token=definition->name;
        }
        return append(planSelectedItem(data,token,131,random,uniques,request.classCode,DropQuality::Unique,false,seeds),random);
    };
    const auto &rules=request.pd2Loot->loot;
    if(!pd2::visitSpecialMonsterDrops(rules,*request.pd2Monster,result.randomState,special)) return result;
    const auto visit=[&](std::string_view treasure,uint64_t &random) {
        const bool original=treasure.empty();
        if(original && entry.status==LootEntryStatus::Empty) return true;
        const auto level=original?entry.itemLevel:std::clamp(request.pd2Monster->stats.get({data.nativeStats->names.at("level"),0}),1,127);
        auto stage=planItemLoot(data,data.tables.at("itemratio"),original?entry.treasureClass:treasure,level,
            original?entry.upgradeLevel:0,random,uniques,request.classCode,request.magicFind,request.goldFind,{},request.effectivePlayers,
            original && request.request.source.ignoreNoDrop,original && request.request.source.identity.monster=="hellbovine",seeds);
        return append(std::move(stage),random);
    };
    const auto area=request.pd2Monster->area;
    if((area<137 || area>210 || pd2::visitMapBonusTreasure(rules,*request.pd2Monster,result.randomState,visit)))
        visit({},result.randomState);
    return result;
}
}
