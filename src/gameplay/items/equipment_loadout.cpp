#include "gameplay/items/equipment_loadout.hpp"
#include "gameplay/items/equipment_requirements.hpp"
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"

namespace d2x {
InventoryError EquipmentLoadout::requirements(EquipmentItem item, const EquipmentActor &actor) const {
    if (!item) return InventoryError::UnknownItem;
    if(skipRequirements) return InventoryError::None;
    return checkEquipmentRequirements(*item.instance, *item.definition, actor,
        [&] { return requirementPercent(*item.instance); });
}
bool EquipmentLoadout::quiverReady(EquipmentItem item,unsigned weaponSet) const {
    if(!item) return false;
    const auto &type=item.definition->equipment.quiver;if(type.empty() || skipRequirements) return true;
    auto other=equipped[size_t(weaponHandSlot(false,weaponSet))];
    if(other && other.instance->id==item.instance->id) other=equipped[size_t(weaponHandSlot(true,weaponSet))];
    return other && other.definition->equipment.isType(type);
}
const ItemInstance *EquipmentLoadout::usable(EquipmentSlot slot, const EquipmentActor &actor) const {
    if (size_t(slot) >= equipped.size()) return nullptr;
    const auto item = equipped[size_t(slot)];
    if (!item || (item.instance->nativeFlags & 0x100u) || (maximumDurability(*item.instance) && !item.instance->durability) ||
        requirements(item, actor) != InventoryError::None) return nullptr;
    return item.instance;
}
EquipmentItem EquipmentLoadout::find(EntityId id) const {
    for (auto item : equipped) if (item && item.instance->id == id) return item;
    return {};
}
} // namespace d2x
