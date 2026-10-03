#pragma once
#include "gameplay/items/equipment_loadout.hpp"

namespace d2x {
class InventoryService;
struct PlayerContainers;
// Authority adapter. The inventory and catalog must outlive this synchronous view.
EquipmentLoadout borrowEquipmentLoadout(const InventoryService &inventory, const PlayerContainers &containers);
} // namespace d2x
