#pragma once
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <map>
#include <string_view>

namespace d2x {
inline int32_t pd2ParameterizedItemStat(const StatSnapshot &stats,uint16_t stat,const WeaponDamage &weapon,
    const EquipmentStats *equipment,const std::map<uint16_t,std::string> &types,bool dualWield) {
    if(!weapon.item) return 0;
    const auto isType=[&](std::string_view type){return std::find(weapon.types.begin(),weapon.types.end(),type)!=weapon.types.end();};
    EntityId other;
    if(equipment) {
        for(const auto &candidate:equipment->weapons) if(candidate.item && candidate.leftHand!=weapon.leftHand) other=candidate.item;
        if(!other && equipment->shield) other=equipment->shield;
    }
    int32_t result{};size_t count{};
    // Current core 0x37F760 copies at most 32 rows. A two-handed weapon held
    // beside another item uses the one-handed contribution on dual wielders.
    for(const auto &[key,value]:stats.values) if(key.id==stat && count++<32 && key.layer) {
        const auto type=types.find(key.layer);if(type==types.end()) continue;
        bool matches=std::find(weapon.typeLayers.begin(),weapon.typeLayers.end(),key.layer)!=weapon.typeLayers.end();
        if(type->second=="2han") matches=matches && (!other || other==weapon.item);
        else if(type->second=="1han") {
            if(!matches && dualWield && isType("2han")) matches=true;
            if(matches) matches=isType("2han")?bool(other) && other!=weapon.item:!other || other!=weapon.item;
        }
        if(matches) result=nativeAdd(result,value);
    }
    return result;
}
}
