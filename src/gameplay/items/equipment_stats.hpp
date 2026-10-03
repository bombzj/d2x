#pragma once
#include "gameplay/combat/weapon_values.hpp"
#include "inventory.hpp"
#include "gameplay/combat/stat_modifiers.hpp"

namespace d2x {
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor, int bonusDefense = 0,
                                    const CombatModifiers &combat = {}, int baseAttackRating = 0);
} // namespace d2x
