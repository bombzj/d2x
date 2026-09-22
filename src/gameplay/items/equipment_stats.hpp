#pragma once
#include "inventory.hpp"

namespace d2x {
struct WeaponDamage {
    EntityId item;
    int minimum = 256, maximum = 512;
    bool ranged = false;
};
struct EquipmentStats {
    std::array<WeaponDamage, 2> weapons;
    int weaponCount = 1;
    int defense = 0, blockChance = 0;
    int level = 1;
};
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor);
} // namespace d2x