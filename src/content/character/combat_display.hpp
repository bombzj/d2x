#pragma once
#include "gameplay/character/attributes.hpp"
#include "gameplay/combat/weapon_values.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/items/modifiers.hpp"
#include <map>
#include <set>
#include <span>

namespace d2x {
struct ClassicData;
// Only complete, observed, active item instances. Slot indices are normalized by
// the wire adapter; the original NOEQUIP/BROKEN flags decide admission there.
struct CombatDisplayItem { EquipmentSlot slot; ItemInstance item; };
struct CombatDisplayEquipment {
    CharacterModifiers modifiers;
    EquipmentStats equipment;
    EquipmentActor actor;
    int concentrationDamagePercent = 0;
};
CombatDisplayEquipment projectCombatEquipment(const ClassicData &, EquipmentActor,
    std::span<const CombatDisplayItem>, std::span<const ItemInstance> charms,
    std::span<const ResolvedItemStat> states, const std::map<int,int> &effectiveRanks, const std::map<int,int> &baseRanks,
    const std::set<int> &activeStates, const std::set<int> &accountedPassives={}, int baseDefense=0, int baseAttackRating=0,
    int baseMinimum=0, int baseMaximum=0);
} // namespace d2x
