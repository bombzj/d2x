#pragma once
#include "gameplay/items/charges.hpp"
#include "gameplay/items/modifiers.hpp"
#include <optional>
#include <span>
#include <algorithm>
#include <stdexcept>

namespace d2x {
struct Pd2ChargeLayer {int layer{},value{};size_t index{};bool runeword{};};
inline const auto &pd2ChargeStatList(const ItemInstance &item) {
    // 0x40 starts at pMyLastList; conditional 0x2040 set lists live on
    // pMyStats. The runeword list is posted after the base magic list.
    return item.runewordRow>=0?item.runewordStats:item.savedStats;
}
inline std::optional<Pd2ChargeLayer> pd2FirstChargeLayer(const ItemInstance &item) {
    std::optional<Pd2ChargeLayer> result;
    const auto &list=pd2ChargeStatList(item);
    for(size_t index=0;index<list.size();++index) if(const auto &stat=list[index];stat.id==204)
        if(!result || stat.parameter<=result->layer) result=Pd2ChargeLayer{stat.parameter,stat.value,index,item.runewordRow>=0};
    return result;
}
inline std::optional<Pd2ChargeLayer> pd2FindChargeLayer(const ItemInstance &item,int layer) {
    // ItemMods::GetItemCharges/UpdateItemWithSkillCharges search backwards
    // through magic lists for this layer, unlike the regeneration callback.
    const auto find=[&](const auto &list,bool runeword)->std::optional<Pd2ChargeLayer> {
        for(size_t index=list.size();index>0;--index) if(const auto &stat=list[index-1];stat.id==204 && stat.parameter==layer)
            return stat.value?std::optional{Pd2ChargeLayer{layer,stat.value,index-1,runeword}}:std::nullopt;
        return {};
    };
    if(item.runewordRow>=0) if(const auto found=find(item.runewordStats,true)) return found;
    return find(item.savedStats,false);
}
inline std::optional<Pd2ChargeLayer> pd2RestoreCharge(ItemInstance &item,int layer) {
    auto selected=pd2FirstChargeLayer(item);
    if(!selected || selected->layer!=layer || selected->value<0 || selected->value>65535 ||
        (selected->value&255)>=((unsigned(selected->value)>>8)&255)) return {};
    auto &list=selected->runeword?item.runewordStats:item.savedStats;
    ++list[selected->index].value;++selected->value;return selected;
}
inline void pd2WriteChargeValues(std::span<ResolvedItemStat> values,const ItemInstance &item,const Pd2ChargeLayer &selected) {
    if(values.empty()) return; // Unused level zero has no prepared properties.
    const auto &list=selected.runeword?item.runewordStats:item.savedStats;
    if(values.size()<list.size()) throw std::logic_error("Missing PD2 charge list projection");
    const auto index=(selected.runeword?values.size()-list.size():0)+selected.index;
    auto &stat=values[index];
    if(stat.name!="item_charged_skill" || stat.layer!=selected.layer)
        throw std::logic_error("PD2 charge projection changed list ownership");
    stat.rawValue=stat.value=selected.value;if(stat.nativeValue) *stat.nativeValue=selected.value;
}
}
