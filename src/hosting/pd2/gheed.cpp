#include "gheed.hpp"
#include "hosting/item_content.hpp"
#include "hosting/character_content.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/cube_data.hpp"
#include "content/pd2/cube.hpp"
#include "server/systems/merchant/system.hpp"
#include "gameplay/items/cube_records.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x::hosting::pd2 {
namespace {
void ringProperty(const ClassicData &data,ItemInstance &item,uint64_t &random,int index,int low,int high) {
    const auto &table=data.tables.at("properties");
    const auto code=table.value(size_t(index),"code");
    if(code.empty()) throw std::runtime_error("Missing native Gheed ring property");
    CubeOutput output;
    output.properties.push_back({{std::string(code),"0",low,high,isDirectPropertyRoll(data,code)},0});
    applyCubeProperties(data,output,item,random);
}
ItemInstance specialRing(const ClassicData &data,int difficulty,uint64_t &gameRandom,uint32_t &crt,
    const std::set<size_t> &uniques,uint64_t &random,std::set<size_t> &limited) {
    auto item=prepareNativeItem(data,"rin",99,difficulty,gameRandom,DropQuality::Unique,uniques,NativeItemUse::Vendor,&random);
    if(item.quality==ItemQuality::Unique && item.specialRow>=0 &&
       !data.tables.at("uniqueitems").number(size_t(item.specialRow),"nolimit").value_or(0)) limited.insert(size_t(item.specialRow));
    // 388CD0 discards the generated property lists, retains the allocated
    // ring, and assigns native file index352 (the table's placeholder name).
    freezeCubeItem(data,item,&random);item.savedStats.clear();
    item.savedSetStats={};item.affixes.clear();item.propertyRolls.clear();item.operationRolls.clear();item.specialRow=352;
    for(const auto row:std::array<std::array<int,3>,4>{{{264,75,75},{255,5,10},{58,50,75},{59,20,30}}})
        ringProperty(data,item,random,row[0],row[1],row[2]);
    const int variant=int(pd2CrtRandom(crt)%6);
    constexpr std::array skills{103,274,275,276,277};
    if(variant<5) ringProperty(data,item,random,skills[size_t(variant)],1,1);
    if(variant<4) {
        constexpr std::array positive{31,35,33,39},negative{35,31,39,33};
        ringProperty(data,item,random,positive[size_t(variant)],30,40);
        ringProperty(data,item,random,negative[size_t(variant)],-10,-20);
    } else {
        if(variant==5) ringProperty(data,item,random,29,30,30);
        ringProperty(data,item,random,variant==4?6:3,6,10);
        ringProperty(data,item,random,41,-3,-5);
    }
    return item;
}
}
void prepareGheedStock(const ClassicData &data,server::merchant::Prepared &result) {
    const auto &source=result.source;
    if(!source.gameRandom || !source.controlRandom || !source.pd2Random) throw std::runtime_error("Missing native Gheed random streams");
    auto gameRandom=*source.gameRandom;auto crt=*source.pd2Random;
    const auto &inventory=data.tables.at("inventory"),&uniques=data.tables.at("uniqueitems");
    for(size_t row=0;row<inventory.rows().size();++row) if(inventory.value(row,"class")=="Monster") {
        result.columns=inventory.number(row,"gridX").value_or(0);result.rows=inventory.number(row,"gridY").value_or(0);break;
    }
    if(result.columns<=0 || result.rows<=0 || uniques.rows().empty()) throw std::runtime_error("Missing native Gheed inventory or unique table");
    server::merchant::Stock shelf;shelf.columns=result.columns;shelf.rows=result.rows;
    auto projection=source.character;projection.inventory.items.clear();
    const auto place=[&](ItemInstance item,uint64_t propertyRandom) {
        const EntityId id{++result.allocatedItems};item.id=id;
        const auto *base=data.items.find(item.definition);if(!base) return false;
        const auto cell=server::merchant::shelfPosition(shelf,*base,data.items,0);if(!cell) return false;
        item.identified=false;item.nativeFlags&=~0x10u;
        auto identified=item;identified.identified=true;
        projection.inventory.items.emplace(id,std::move(identified));
        result.offers.push_back({std::move(item),{},false,{},0,*cell,propertyRandom});
        shelf.offers.emplace(id,result.offers.back());return true;
    };
    auto occupied=source.uniques;
    if(pd2CrtRandom(crt)%200==0) {
        uint64_t random{};
        auto item=specialRing(data,source.difficulty,gameRandom,crt,occupied,random,result.limitedUniques);
        place(std::move(item),random);
        occupied.insert(result.limitedUniques.begin(),result.limitedUniques.end());
    }
    const auto select=[&]()->std::optional<size_t> {
        for(int attempt=0;attempt<10;++attempt) {
            const auto row=size_t(pd2CrtRandom(crt)%uniques.rows().size());
            const auto required=uint16_t(uniques.number(row,"lvl req").value_or(0));
            const bool gate=required<60 || int(pd2CrtRandom(crt)%100)>=int(required)-49;
            uint32_t flags{};
            for(const auto &[name,bit]:std::array<std::pair<std::string_view,unsigned>,4>{{{"enabled",0},{"nolimit",1},{"carry1",2},{"ladder",3}}})
                if(uniques.number(row,name).value_or(0)) flags|=1u<<bit;
            const auto level=uint16_t(uniques.number(row,"lvl").value_or(0));
            if(gate && uniques.value(row,"code")!="jew" && flags && uniques.number(row,"rarity").value_or(0) && level && level<100) return row;
        }
        return {};
    };
    auto selected=select();
    for(int attempt=0;attempt<20 && selected && result.offers.size()<5;++attempt) {
        const auto record=std::find_if(data.uniqueItems.begin(),data.uniqueItems.end(),[&](const auto &value){return value.row==*selected;});
        // The forced creator still resolves the current base item and table
        // row; absent resources cannot be replaced by another unique.
        if(record==data.uniqueItems.end() || !data.items.find(record->code)) break;
        auto used=occupied;used.erase(record->row);
        pd2CrtRandom(crt);pd2CrtRandom(crt); // 3841D0's temporary seed.
        uint64_t random{};
        auto item=prepareNativeItem(data,record->name,99,source.difficulty,gameRandom,DropQuality::Unique,used,NativeItemUse::Vendor,&random);
        if(place(std::move(item),random)) selected=select();
    }
    result.equipment=prepareEquipmentRules(data,projection);
    for(auto &offer:result.offers) offer.equipment=result.equipment->items.at(offer.item.id);
    result.gameRandom=gameRandom;result.controlRandom=source.controlRandom;result.pd2Random=crt;
}
void prepareGheedPurchase(const ClassicData &data,server::merchant::Prepared &result) {
    const auto &source=result.source;const auto requested=source.pending.request.item;
    if(!requested || !source.pd2Random) return;
    const auto found=source.stock.offers.find(requested->id);if(found==source.stock.offers.end()) return;
    auto item=found->second.item;item.identified=true;
    auto random=found->second.propertyRandom;auto crt=*source.pd2Random;
    const bool corrupted=applyPd2AutomaticCorruption(data,item,random,crt);
    auto projection=source.character;projection.inventory.items.clear();projection.inventory.items.emplace(item.id,item);
    result.purchase=std::move(item);result.equipment=prepareEquipmentRules(data,projection);
    if(corrupted) result.pd2Random=crt;
}
}
