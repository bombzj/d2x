#pragma once
#include "weapon_values.hpp"
#include "gameplay/stats/stat_list.hpp"
#include <algorithm>

namespace d2x {
// Same parameter-layer walk for melee and missile GetResultFlags. The native
// buffer admits at most 32 passive_weaponblock entries.
inline int nativeWeaponBlockChance(const EquipmentStats &equipment,const StatSnapshot &stats,const StatRuleCatalog &rules) {
    const auto id=rules.names.at("passive_weaponblock");size_t count{};int result{};
    for(const auto &[key,value]:stats.values) if(key.id==id && count++<32) {
        const auto matches=[layer=key.layer](const WeaponDamage &weapon) {
            return weapon.item && std::find(weapon.typeLayers.begin(),weapon.typeLayers.end(),layer)!=weapon.typeLayers.end();
        };
        if(!key.layer || ((matches(equipment.weapons[0]) || matches(equipment.weapons[1])) && value>result)) result=value;
    }
    return result;
}
}
