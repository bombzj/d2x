#pragma once
#include "gameplay/combat/weapon_values.hpp"

namespace d2x {
// Current core 330B90 -> D2Game 372C0 (PLAYER::sub_6FC7C7B0).
// Equipment contains only currently usable weapons; a failed skill selection
// falls back to the first equipped weapon, never to an inactive weapon set.
inline WeaponDamage pd2SkillWeapon(const EquipmentStats &equipment,bool dualWield,
    std::optional<int> selection,bool secondary,uint32_t sequenceFrame) {
    const WeaponDamage *right=nullptr,*left=nullptr;
    for(const auto &weapon:equipment.weapons) if(weapon.item)
        (weapon.leftHand?left:right)=&weapon;
    const auto *fallback=right?right:left;
    if(!dualWield || !selection) return fallback?*fallback:equipment.weapons[0];
    const WeaponDamage *selected=nullptr;
    switch(*selection) {
    case 1:selected=left;break;
    case 2:selected=!right || (left && secondary)?left:right;break;
    case 3:selected=sequenceFrame%2?left:right;break;
    case 4:return {};
    default:selected=right?right:left;break;
    }
    return selected?*selected:fallback?*fallback:equipment.weapons[0];
}
}
