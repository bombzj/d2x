#pragma once
#include "container_grid.hpp"
#include "client/inventory_client.hpp"
#include "gameplay/items/operations.hpp"
#include "presentation/hud/hud_layout.hpp"
#include "presentation/hud/classic_panel.hpp"

namespace d2x {
struct InventoryDrag {
    ItemHandle item;
    Cell grab;
    Vec pressedAt, pixelOffset;
    bool moved = false;
    bool pickedUp = false;
    bool onCursor = false;
};
// Presentation only: keep the picked-up source hidden until native
// cursor/placement facts arrive, without predicting a destination inventory.
struct InventoryPendingPlacement {
    EntityId item;
    ItemLocation source;
};
struct SplitDialog {
    ItemHandle item;
    unsigned quantity = 1;
    EntityId destination;
};
struct GoldDialog {
    GoldAction action = GoldAction::Drop;
    std::string amount;
    unsigned maximum = 0;
};
struct InventoryUi {
    bool open = false, cubeOpen = false, forceSwap = false, beltExpanded = false, playerTradeOpen = false;
    EntityId selected, pending, storage;
    std::string pendingMessage;
    std::optional<InventoryDrag> drag;
    std::optional<InventoryPendingPlacement> pendingPlacement;
    std::optional<SplitDialog> split;
    std::optional<ItemHandle> identify;
    uint64_t targetingRevision{}, targetingGeneration{};
    std::optional<GoldDialog> goldDialog;
    void syncCursor(const InventoryView &inventory, EntityId reserved = {});
    bool hidesItem(const InventoryItemView &item) const {
        return (drag && drag->item.id==item.id) ||
            (pending && pendingPlacement && pendingPlacement->item==pending &&
             item.id==pendingPlacement->item && item.location==pendingPlacement->source);
    }
    void cancelGesture() {
        if (drag && !drag->onCursor) drag.reset();
        split.reset();
        identify.reset();
        goldDialog.reset();
    }
};
// Classic invchar.dc6's right panel, scaled from 320 x 432; grid data from inventory.txt.
inline constexpr float inventoryScale = classicPanelScale, inventoryCellSize = 29 * inventoryScale;
inline Rectangle orificeBounds() {return {106*inventoryScale,98*inventoryScale,108*inventoryScale,164*inventoryScale};}
inline Rectangle orificeSlot() {const auto p=orificeBounds();return {p.x+16*inventoryScale,p.y+12*inventoryScale,76*inventoryScale,108*inventoryScale};}
inline Rectangle orificeButton(bool confirm) {const auto p=orificeBounds();return {p.x+(confirm?10:66)*inventoryScale,p.y+126*inventoryScale,32*inventoryScale,32*inventoryScale};}
inline Rectangle inventoryBounds() {
    return classicPanelBounds(true);
}
inline Rectangle inventoryGrid() {
    auto p = inventoryBounds();
    return {p.x + 19 * inventoryScale, p.y + 255 * inventoryScale, 10 * inventoryCellSize,
            4 * inventoryCellSize};
}
inline Rectangle inventoryClose() {
    auto p = inventoryBounds();
    return {p.x + 19 * inventoryScale, p.y + 389 * inventoryScale,
            32 * inventoryScale, 32 * inventoryScale};
}
inline Rectangle inventoryGold() {
    auto p = inventoryBounds();
    return {p.x + 82 * inventoryScale, p.y + 391 * inventoryScale,
            120 * inventoryScale, 20 * inventoryScale};
}
inline Rectangle inventoryToggle() {
    return hudMenuButton();
}
inline Rectangle equippedBeltBounds() {
    auto p = inventoryBounds();
    return {p.x + 136 * inventoryScale, p.y + 179 * inventoryScale, 52 * inventoryScale, 25 * inventoryScale};
}
Rectangle equipmentBounds(EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentAt(Vec mouse, unsigned weaponSet);
Rectangle weaponTabBounds(unsigned set, bool left);
std::optional<unsigned> weaponTabAt(Vec mouse);
inline Rectangle beltSlot(Cell cell) {
    return hudBeltSlot(cell.x, cell.y);
}
inline Rectangle beltBounds(int rows) {
    return hudRect(424, 42 + (rows - 1) * 32, 125, rows * 32);
}
inline std::optional<Cell> beltCell(Vec mouse, int rows) {
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < 4; ++x)
            if (CheckCollisionPointRec(rv(mouse), beltSlot({x, y})))
                return Cell{x, y};
    return std::nullopt;
}
inline Rectangle storageBounds() {
    return classicPanelBounds(false);
}
inline Rectangle storageClose() {
    return {storageBounds().x + 274 * inventoryScale, storageBounds().y + 384 * inventoryScale,
            31 * inventoryScale, 34 * inventoryScale};
}
inline Rectangle cubeBounds() {
    return classicPanelBounds(false);
}
inline Rectangle cubeClose() {
    auto p = cubeBounds();
    return {p.x + 272 * inventoryScale, p.y + 383 * inventoryScale,
            32 * inventoryScale, 32 * inventoryScale};
}
inline Rectangle cubeTransmute() {
    auto p = cubeBounds();
    return {p.x + 109 * inventoryScale, p.y + 297 * inventoryScale,
            102 * inventoryScale, 35 * inventoryScale};
}
inline Rectangle storageGold(bool expansion) {
    auto p = storageBounds();
    return {p.x + 74 * inventoryScale,
            p.y + (expansion ? 20 : 218) * inventoryScale,
            176 * inventoryScale, 22 * inventoryScale};
}
inline Rectangle goldDialogBounds() {
    return {inventoryBounds().x + 28, 192, 344, 166};
}
inline Rectangle goldDialogButton(int index) {
    auto p = goldDialogBounds();
    return {p.x + 23 + index * 166, p.y + 123, 130, 29};
}
inline Rectangle splitBounds() {
    return {inventoryBounds().x + 24, 190, 352, 180};
}
inline Rectangle splitButton(int index) {
    auto p = splitBounds();
    return {p.x + 22 + index * 165, p.y + 138, 143, 28};
}
inline Rectangle splitAdjust(int index) {
    auto p = splitBounds();
    return {p.x + 32 + index * 250, p.y + 60, 38, 32};
}
std::optional<Cell> inventoryCell(Vec mouse);
Rectangle inventoryItemBounds(Cell cell, const ItemDefinition &definition);
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
                            bool hirelingOpen = false);
} // namespace d2x
