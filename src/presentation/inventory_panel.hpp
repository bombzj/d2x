#pragma once
#include "container_grid.hpp"
#include "gameplay/session/session.hpp"
#include "hud_layout.hpp"

namespace d2x {
struct InventoryDrag {
    ItemHandle item;
    Cell grab;
    Vec pressedAt, pixelOffset;
    bool moved = false;
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
    bool open = false, cubeOpen = false, forceSwap = false, beltExpanded = false;
    EntityId selected, pending, storage;
    std::string pendingMessage;
    std::optional<InventoryDrag> drag;
    std::optional<SplitDialog> split;
    std::optional<ItemHandle> identify;
    std::optional<GoldDialog> goldDialog;
    void cancelGesture() {
        drag.reset();
        split.reset();
        identify.reset();
        goldDialog.reset();
    }
};
// Classic invchar.dc6's right panel, scaled from 320 x 432; grid data from inventory.txt.
inline constexpr float inventoryScale = 1.25f, inventoryCellSize = 29 * inventoryScale;
inline Rectangle inventoryBounds() {
    return {W - 416.f, 20, 400, 540};
}
inline Rectangle inventoryGrid() {
    auto p = inventoryBounds();
    return {p.x + 19 * inventoryScale, p.y + 255 * inventoryScale, 10 * inventoryCellSize,
            4 * inventoryCellSize};
}
inline Rectangle inventoryClose() {
    auto p = inventoryBounds();
    return {p.x + 14 * inventoryScale, p.y + 381 * inventoryScale,
            33 * inventoryScale, 30 * inventoryScale};
}
inline Rectangle inventoryGold() {
    auto p = inventoryBounds();
    return {p.x + 82 * inventoryScale, p.y + 381 * inventoryScale,
            120 * inventoryScale, 26 * inventoryScale};
}
inline Rectangle inventoryToggle() {
    return hudMenuButton();
}
inline Rectangle equippedBeltBounds() {
    auto p = inventoryBounds();
    return {p.x + 136 * inventoryScale, p.y + 179 * inventoryScale, 52 * inventoryScale, 25 * inventoryScale};
}
Rectangle equipmentBounds(EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentAt(Vec mouse);
inline Rectangle beltSlot(Cell cell) {
    return hudRect(425 + 31 * cell.x, 41 + 32 * cell.y, 29, 29);
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
    return {16, 20, 400, 540};
}
inline Rectangle storageClose() {
    return {storageBounds().x + 274 * inventoryScale, storageBounds().y + 384 * inventoryScale,
            31 * inventoryScale, 34 * inventoryScale};
}
inline Rectangle cubeBounds() {
    return {inventoryBounds().x - 400, inventoryBounds().y, 400, 540};
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
inline Rectangle storageGold() {
    auto p = storageBounds();
    return {p.x + 84 * inventoryScale, p.y + 378 * inventoryScale,
            176 * inventoryScale, 28 * inventoryScale};
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
    std::optional<GameCommand> command;
    InventoryError error = InventoryError::None;
    std::string description;
    Rectangle bounds{};
    std::optional<Rectangle> otherBounds;
};
std::vector<ContainerGrid> inventoryGrids(const GameSession &session, const InventoryUi &ui);
bool inventorySurface(const InventoryUi &ui, Vec mouse);
InventoryDrop inventoryDrop(const GameSession &session, const InventoryUi &ui, Vec mouse);
} // namespace d2x
