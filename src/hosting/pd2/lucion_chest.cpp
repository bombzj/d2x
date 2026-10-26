#include "lucion_chest.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_quality.hpp"
#include "server/systems/loot/system.hpp"
#include "gameplay/maps/pd2/definition.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
LootPlan planPd2LucionChest(const ClassicData &data,const server::loot::Preparation &request,const ItemSeedSource &seeds) {
    if(!request.pd2Map || request.pd2Map->controller!=45 || !request.pd2Map->catalog)
        throw std::runtime_error("Missing native Lucion chest controller");
    LootPlan result;result.randomState=request.seed;auto uniques=request.uniques;
    // 41d49 3A9480 -> 37F430: MonStats967.TreasureClass1 plus these
    // compiled TC offsets. DropTC resets its output count on each call.
    for(size_t stage=0;stage<2;++stage) {
        const auto &tables=request.pd2Map->catalog->relativeTreasures;
        const size_t offset=1037+stage;
        if(offset>=tables.size()) throw std::runtime_error("Missing native Lucion chest treasure");
        auto next=planItemLoot(data,data.tables.at("itemratio"),tables[offset],std::max(1,int(request.character.player.level)),0,
            result.randomState,uniques,request.classCode,request.magicFind,request.goldFind,{},request.effectivePlayers,false,false,seeds,
            stage?size_t(2+request.pd2Map->bossDifficulty):3);
        if(!next.deferred.empty()) {result.deferred=std::move(next.deferred);return result;}
        result.randomState=next.randomState;result.noDrops+=next.noDrops;
        for(auto &drop:next.drops) {
            if(drop.generation.quality==ItemQuality::Unique && drop.generation.specialRow>=0 &&
               !data.tables.at("uniqueitems").number(size_t(drop.generation.specialRow),"nolimit").value_or(0))
                uniques.insert(size_t(drop.generation.specialRow));
            result.drops.push_back(std::move(drop));
        }
    }
    return result;
}
}
