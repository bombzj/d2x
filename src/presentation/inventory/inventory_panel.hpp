#pragma once
#include "container_grid.hpp"
#include "client/inventory_client.hpp"
#include "gameplay/items/operations.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/hud/classic_panel.hpp"
#include <array>

namespace d2x {
struct InventoryDrag {
    ItemHandle item;
    Vec pressedAt;
    bool moved = false;
    bool pickedUp = false;
    bool onCursor = false;
    std::optional<Vec> releasedAt{};
};
// Presentation only: keep the picked-up source hidden until native
// cursor/placement facts arrive, without predicting a destination inventory.
struct InventoryPendingPlacement {
    EntityId item;
    ItemLocation source;
    bool taking = false;
};
struct GoldDialog {
    GoldAction action = GoldAction::Drop;
    std::string amount;
    unsigned maximum = 0;
    size_t caret = amount.size(), anchor = 0;
    bool selecting = false;
};
struct InventoryUi {
    bool open = false, cubeOpen = false, beltExpanded = false, beltHovered = false, playerTradeOpen = false;
    EntityId selected, pending, storage;
    std::string pendingMessage;
    std::optional<InventoryDrag> drag;
    std::optional<InventoryPendingPlacement> pendingPlacement;
    std::optional<ItemHandle> identify;
    uint64_t targetingRevision{}, targetingGeneration{};
    std::optional<GoldDialog> goldDialog;
    std::optional<unsigned> storagePressedButton;
    void syncCursor(const InventoryView &inventory, EntityId reserved = {});
    int visibleBeltRows(int capacityRows) const {
        return beltExpanded || beltHovered ? capacityRows : 1;
    }
    bool hidesItem(const InventoryItemView &item) const {
        return (drag && drag->item.id==item.id) ||
            (pending && pendingPlacement && pendingPlacement->item==pending &&
             item.id==pendingPlacement->item && item.location==pendingPlacement->source);
    }
    void cancelGesture() {
        if (drag && !drag->onCursor) drag.reset();
        else if (drag) drag->releasedAt.reset();
        identify.reset();
        goldDialog.reset();
        storagePressedButton.reset();
    }
};
// Classic invchar.dc6's right panel, scaled from 320 x 432; grid data from inventory.txt.
float inventoryScale();
float inventoryCellSize(const InventoryView &inventory);
Rectangle orificeBounds();
Rectangle orificeSlot();
Rectangle orificeButton(bool confirm);
Rectangle inventoryBounds();
Rectangle inventoryBounds(const InventoryView &inventory);
Rectangle inventoryGrid(const InventoryView &inventory);
Rectangle inventoryClose();
Rectangle inventoryGold();
Rectangle inventoryGoldButton();
Rectangle inventoryClose(const InventoryView &inventory);
Rectangle inventoryGold(const InventoryView &inventory);
Rectangle inventoryGoldButton(const InventoryView &inventory);
Rectangle inventoryToggle();
Rectangle equipmentBounds(const InventoryView &inventory, EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentAt(const InventoryView &inventory, Vec mouse, unsigned weaponSet);
Rectangle weaponTabBounds(unsigned set, bool left);
std::optional<unsigned> weaponTabAt(Vec mouse);
Rectangle beltSlot(Cell cell);
Rectangle beltBounds(int rows);
std::optional<Cell> beltCell(Vec mouse, int rows);
Rectangle storageBounds();
Rectangle storageBounds(const InventoryView &inventory);
// mdll1 0x10980-0x10D80: native baseline = panel bottom minus Y offset.
// Five top buttons (shared, index-, page-, page+, index+), bottom materials.
Rectangle storageNavigationButton(unsigned button, const InventoryView &inventory);
Rectangle storageMaterialBounds(const InventoryMaterialView &item);
Rectangle storageMaterialUpgradeBounds(const InventoryMaterialView &item);
Rectangle storageClose();
Rectangle storageClose(const InventoryView &inventory);
Rectangle cubeBounds();
Rectangle cubeClose(const InventoryView &inventory);
Rectangle cubeTransmute();
Rectangle storageGold(bool expansion);
Rectangle storageGoldButton(bool expansion);
Rectangle storageGold(const InventoryView &inventory);
Rectangle storageGoldButton(const InventoryView &inventory);
Rectangle storageNameField(const InventoryView &inventory);
Rectangle goldDialogBounds();
Rectangle goldDialogButton(int index);
Rectangle goldDialogEntry();
std::optional<Cell> inventoryCell(const InventoryView &inventory, Vec mouse);
struct InventoryDrop {
    std::optional<InventoryIntent> command;
    InventoryError error = InventoryError::None;
    std::string description;
    Rectangle bounds{};
    std::optional<Rectangle> otherBounds;
};
std::vector<ContainerGrid> inventoryGrids(const InventoryView &inventory, const InventoryUi &ui);
ContainerGrid playerTradeGrid(const InventoryView &, bool own);
bool inventorySurface(const InventoryUi &ui, Vec mouse);
InventoryDrop inventoryDrop(const InventoryView &inventory, const IInventoryClient &client, const InventoryUi &ui, Vec mouse,
                            std::array<int, 2> cursorPixels, bool hirelingOpen = false);
} // namespace d2x
