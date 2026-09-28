#pragma once
#include "gameplay/items/definitions.hpp"

namespace d2x {
inline int equippedHandComponent(const ItemDefinition &item, bool leftHand) {
    const auto &weaponClass = item.base.weaponClass;
    // D2MOO INVENTORY_GetCompositItem only remaps these one-handed weapon classes.
    // Other weapons retain their MPQ component: a bow uses LH even in the main slot.
    if (item.equipment.isType("weap") &&
        (weaponClass == "1hs" || weaponClass == "1ht" || weaponClass == "ht1"))
        return leftHand ? 6 : 5;
    return item.appearance.component;
}
} // namespace d2x
