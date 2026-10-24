#include "item_reroll.hpp"
#include "hosting/item_content.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/cube_data.hpp"
#include "content/items/socket_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ItemInstance rerollPd2DemonicItem(const ClassicData &data,const ItemInstance &original,uint64_t &random,int difficulty) {
    // Current core 0x365200 recreates the same unique/set file index, then
    // restores the upgraded base, sockets and the selected native base stats.
    if(original.quality!=ItemQuality::Unique && original.quality!=ItemQuality::Set)
        throw std::runtime_error("PD2 demonic cube requires a unique or set item");
    const auto &records=original.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
    const auto source=std::find_if(records.begin(),records.end(),[&](const auto &record){return int(record.row)==original.specialRow;});
    if(source==records.end()) throw std::runtime_error("PD2 demonic cube item file index is unavailable");
    const auto rolled=rollItemProperties(data,source->properties,random,int(original.level));random=rolled.randomState;
    ItemGeneration generation;generation.quality=original.quality;generation.specialRow=original.specialRow;
    generation.requiredLevel=source->requiredLevel;generation.propertyRolls=rolled.values;generation.operationRolls=rolled.operations;
    auto item=prepareItem(data,{source->code,1,{},original.level,std::move(generation)},random,difficulty);
    item.definition=original.definition;item.identified=true;
    item.nativeFlags=(item.nativeFlags&~0x400000u)|(original.nativeFlags&0x400000u);
    item.quantity=original.quantity;item.defense=original.defense;
    item.durability=original.durability;item.nativeMaxDurability=original.nativeMaxDurability;
    item.sockets=original.sockets;item.socketedItems=original.socketedItems;item.socketRequiredLevel=original.socketRequiredLevel;
    if(item.sockets) item.nativeFlags|=0x800u;else item.nativeFlags&=~0x800u;
    const auto copy=[&](int id) {
        const auto source=std::find_if(original.savedStats.begin(),original.savedStats.end(),[&](const auto &stat){return stat.id==id && !stat.parameter;});
        if(source==original.savedStats.end() || !source->value) return;
        std::erase_if(item.savedStats,[&](const auto &stat){return stat.id==id && !stat.parameter;});item.savedStats.push_back(*source);
    };
    copy(91);copy(270);
    const auto indestructible=std::find_if(original.savedStats.begin(),original.savedStats.end(),[](const auto &stat){return stat.id==152 && !stat.parameter && stat.value;});
    if(indestructible!=original.savedStats.end() || (original.nativeFlags&0x400000u)) {
        std::erase_if(item.savedStats,[](const auto &stat){return stat.id==152 && !stat.parameter;});
        if(indestructible!=original.savedStats.end()) item.savedStats.push_back(*indestructible);
    }
    if(original.nativeFlags&0x100u) item.nativeFlags|=0x100u;
    prepareSocketedItem(data,item,random);updateCubeRequiredLevel(data,item);
    return item;
}
}
