#pragma once
#include "inventory.hpp"

namespace d2x {
struct WeaponDamage {
    EntityId item;
    int minimum = 256, maximum = 512;
    bool ranged = false;
    bool throwable = false;
    bool leftHand = false;
    int throwMinimum = 0, throwMaximum = 0;
    int missileId = -1;
    float missileSpeed = 0, missileLifetime = 0;
};
struct EquipmentStats {
    std::array<WeaponDamage, 2> weapons;
    int weaponCount = 1;
    int defense = 0, blockChance = 0;
    int level = 1;
};
EquipmentStats deriveEquipmentStats(const InventoryService &inventory, const PlayerContainers &containers,
                                    const EquipmentActor &actor, int bonusDefense = 0);
} // namespace d2x
