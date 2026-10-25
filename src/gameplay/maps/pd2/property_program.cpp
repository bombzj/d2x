#include "property_program.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2x::pd2 {
std::vector<StatValue> prepareMapSunderProperties(const MapPropertyRules &rules,const MapProperties &properties,int area,const StatSnapshot &base) {
    std::vector<StatValue> result;
    for(const auto &program:properties.global) if(program.key.id==10) {
        if(program.value!=area || !program.key.layer) return result;
        const int32_t boundary=program.key.layer;
        int32_t previous=0;
        for(const auto id:rules.sunderResistances) {
            const auto sum=nativeAdd(base.get({id,0}),boundary);
            // Preserve the native CMOVLE chain: a nonpositive channel retains
            // the previous channel's adjustment, rather than independently
            // inventing a conventional immunity cap.
            const auto value=sum>boundary?nativeSubtract(boundary,sum):previous;
            result.emplace_back(StatKey{id,0},value);previous=value;
        }
        break;
    }
    return result;
}
int32_t mapGlobalValue(const MapProperties &properties,uint16_t program) {
    for(const auto &value:properties.global) if(value.key.id==program) return value.value;
    return 0;
}
MapProperties prepareMapProperties(const MapPropertyRules &rules,std::span<const StatValue> source) {
    MapProperties result;
    std::vector<StatValue> physical;
    result.original.assign(source.begin(),source.end());
    for(const auto &value:source) {
        const auto found=rules.transfers.find(value.key.id);
        if(found==rules.transfers.end()) throw std::invalid_argument("Missing PD2 map stat transfer");
        const auto divide=found->second.divide;
        if(!divide) {result.forceEvent|=value.key.id==rules.forceEvent;continue;}
        auto transferred=value;
        if(divide==1024) result.monsters.push_back(value);
        else {
            transferred.key.id=uint16_t(divide%1000);
            switch(divide/1000) {
            case 1:result.players.push_back(transferred);break;
            case 2:result.global.push_back(transferred);break;
            case 3:physical.push_back(value);break;
            default:result.monsters.push_back(transferred);break;
            }
        }
    }
    result.monsters.insert(result.monsters.end(),physical.begin(),physical.end());
    return result;
}
namespace {
int32_t physicalPercent(int32_t base,int32_t percent) {
    // Current 0x388C44 uses signed CVTDQ2PS, DIVSS, MULSS and CVTTSS2SI.
    const float ratio=float(percent)/100.f;
    const float value=ratio*float(uint32_t(base));
    if(!std::isfinite(value) || value>=2147483648.f || value<-2147483648.f) return INT32_MIN;
    return int32_t(std::trunc(value));
}
}
MapUnitProperties prepareMapUnitProperties(const MapPropertyRules &rules,const MapProperties &properties,
    bool player,int32_t minimum,int32_t maximum,int32_t maximumLife) {
    MapUnitProperties result;
    std::vector<StatValue> extra;
    const auto append=[&](StatValue value) {
        // STATLIST_SetStat replaces the same stat/layer, rather than adding
        // another list contribution when two map properties translate alike.
        const auto existing=std::find_if(result.values.begin(),result.values.end(),[&](const auto &entry){return entry.key==value.key;});
        if(existing==result.values.end()) result.values.push_back(value);else existing->value=value.value;
    };
    for(auto value:player?properties.players:properties.monsters) {
        const auto element=rules.physicalElements.find(value.key.id);
        if(element!=rules.physicalElements.end()) {
            const auto &ids=element->second;
            auto low=physicalPercent(minimum,value.value);
            append({{ids[0],0},low?low:1});
            extra.emplace_back(StatKey{ids[1],0},physicalPercent(maximum,value.value));
            if(ids[2]!=UINT16_MAX) extra.emplace_back(StatKey{ids[2],0},value.key.layer?int32_t(value.key.layer):ids[2]==56?25:125);
            continue;
        }
        if(!player && value.key.id==76) {
            // The native base HP write is unsigned division before multiply.
            auto next=uint32_t(maximumLife)+uint32_t(maximumLife)/100*uint32_t(value.value);
            result.maximumLife=int32_t(std::min(next,uint32_t(INT32_MAX)));
            maximumLife=*result.maximumLife;
            append({{432,0},1}); // Native HP adjustment marker in state198.
            continue;
        }
        if(!value.value) value.value=1;
        append(value);
    }
    // The native temporary vector is append-only; its SetStat pass happens
    // after every direct write, preserving last-write semantics for maxima.
    for(const auto &value:extra) append(value);
    return result;
}
}
