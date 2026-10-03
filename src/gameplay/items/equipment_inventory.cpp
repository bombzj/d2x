#include "gameplay/items/equipment_inventory.hpp"
#include "gameplay/items/inventory.hpp"

namespace d2x {
EquipmentLoadout borrowEquipmentLoadout(const InventoryService &inventory, const PlayerContainers &containers) {
    EquipmentLoadout result;
    auto bind = [&](EntityId id) {
        const auto *item = inventory.item(id);
        return EquipmentItem{item, item ? inventory.catalog().find(item->definition) : nullptr};
    };
    for (size_t index = 0; index < result.equipped.size(); ++index)
        result.equipped[index] = bind(inventory.equipped(containers, EquipmentSlot(index)));
    for (auto id : inventory.contents(containers.backpack)) result.backpack.push_back(bind(id));
    result.requirementPercent = [&inventory](const ItemInstance &item) {
        return inventory.propertyValue(item, "item_req_percent");
    };
    return result;
}
} // namespace d2x
