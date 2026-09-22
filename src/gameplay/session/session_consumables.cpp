#include "gameplay/session/session.hpp"

namespace d2x {
void GameSession::useItem(ItemHandle handle) {
    auto error = previewInventory(UseItem{handle});
    if (error != InventoryError::None) {
        simulation_.emit(InventoryRejected{handle.id, error});
        return;
    }
    auto code = inventory_.item(handle.id)->definition;
    auto potion = *potionDefinition(code);
    auto result = inventory_.drink(handle, inventoryAccess());
    bool consumed = bool(result);
    publishInventory(std::move(result), handle.id);
    if (consumed) {
        simulation_.applyPotion(potion);
        simulation_.emit(ItemUsed{handle.id, std::move(code)});
    }
}
void GameSession::useBeltColumn(int column) {
    auto belt = inventory_.container(playerContainers_.belt);
    if (!belt || column < 0 || column >= belt->spec.columns) {
        simulation_.emit(InventoryRejected{{}, InventoryError::InvalidRequest});
        return;
    }
    // A manually rearranged column may have a hole; use its lowest occupied cell.
    for (int row = 0; row < belt->spec.rows; ++row)
        if (auto item = inventory_.item(inventory_.itemAt(belt->id, {column, row}))) {
            useItem(item->handle());
            return;
        }
    simulation_.emit(PickupFailed{{}, "That belt column is empty."});
}
} // namespace d2x
