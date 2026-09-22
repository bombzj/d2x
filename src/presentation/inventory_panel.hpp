#pragma once
#include "container_grid.hpp"
#include "gameplay/session.hpp"
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
struct InventoryUi {
    bool open = false, forceSwap = false, beltExpanded = false;
    EntityId selected, pending, storage;
    std::string pendingMessage;
    std::optional<InventoryDrag> drag;
    std::optional<SplitDialog> split;
    void cancelGesture() {
        drag.reset();
        split.reset();
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
    return {p.x + p.width - 34, p.y + 9, 23, 23};
}
inline Rectangle inventoryButton(int index) {
    return {inventoryBounds().x + 19 + index * 123, 501, 116, 28};
}
inline Rectangle inventoryToggle() {
    return hudMenuButton();
}
inline Rectangle equippedBeltBounds() {
    auto p = inventoryBounds();
    return {p.x + 136 * inventoryScale, p.y + 179 * inventoryScale, 52 * inventoryScale, 25 * inventoryScale};
}
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
inline Rectangle storageTransfer() {
    return {storageBounds().x + 90, 510, 220, 28};
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
