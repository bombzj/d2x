#include "contracts/inventory.hpp"

namespace d2x {
const InventoryItemView *InventoryView::item(EntityId id) const {
    const auto found = items.find(id);
    return found == items.end() ? nullptr : &found->second;
}
const InventoryContainerView *InventoryView::container(EntityId id) const {
    const auto found = containerViews.find(id);
    return found == containerViews.end() ? nullptr : &found->second;
}
const InventoryDefinitionView *InventoryView::definition(std::string_view code) const {
    const auto found = definitions.find(code);
    return found == definitions.end() ? nullptr : &found->second;
}
EntityId InventoryView::itemAt(EntityId containerId, Cell cell) const {
    const auto *grid = container(containerId);
    if (!grid || cell.x < 0 || cell.y < 0 || cell.x >= grid->columns || cell.y >= grid->rows) return {};
    for (const auto &[id, value] : items) {
        const auto *location = std::get_if<ContainerLocation>(&value.location);
        if (!location || location->container != containerId) continue;
        if (grid->kind == ContainerKind::Equipment) {
            if (location->cell == cell) return id;
            continue;
        }
        const auto *size = definition(value.definition);
        if (size && cell.x >= location->cell.x && cell.y >= location->cell.y &&
            cell.x < location->cell.x + size->width && cell.y < location->cell.y + size->height)
            return id;
    }
    return {};
}
std::vector<EntityId> InventoryView::contents(EntityId containerId) const {
    std::vector<EntityId> result;
    for (const auto &[id, value] : items)
        if (const auto *location = std::get_if<ContainerLocation>(&value.location);
            location && location->container == containerId) result.push_back(id);
    return result;
}
EntityId InventoryView::equipped(const PlayerContainers &owned, EquipmentSlot slot) const {
    return slot == EquipmentSlot::Belt ? itemAt(owned.beltEquipment, {})
        : itemAt(owned.equipment, {int(slot), 0});
}
const InventoryItemView *InventoryView::cursorItem() const {
    const auto held = contents(containers.cursor);
    return held.empty() ? nullptr : item(held.front());
}
} // namespace d2x
