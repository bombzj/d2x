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
    void syncCursor(const InventoryView &inventory, EntityId reserved = {});
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
    }
};
// Classic invchar.dc6's right panel, scaled from 320 x 432; grid data from inventory.txt.
inline float inventoryScale() { return classicPanelScale(); }
inline float inventoryCellSize(const InventoryView &inventory) { return inventory.itemCellSize[0] * inventoryScale(); }
inline Rectangle orificeBounds() {return {106*inventoryScale(),98*inventoryScale(),108*inventoryScale(),164*inventoryScale()};}
inline Rectangle orificeSlot() {const auto p=orificeBounds();return {p.x+16*inventoryScale(),p.y+12*inventoryScale(),76*inventoryScale(),108*inventoryScale()};}
inline Rectangle orificeButton(bool confirm) {const auto p=orificeBounds();return {p.x+(confirm?10:66)*inventoryScale(),p.y+126*inventoryScale(),32*inventoryScale(),32*inventoryScale()};}
inline Rectangle inventoryBounds() {
    return classicPanelBounds(true);
}
inline Rectangle inventoryBounds(const InventoryView &inventory) {
    auto panel = classicPanelBounds(true, float(inventory.panelTop));
    panel.height = inventory.panelHeight * inventoryScale();
    return panel;
}
inline Rectangle inventoryGrid(const InventoryView &inventory) {
    auto p = inventoryBounds(inventory);
    const auto &layout = inventory.backpackLayout;
    return {p.x + layout.left * inventoryScale(), p.y + layout.top * inventoryScale(),
            layout.columns * inventoryCellSize(inventory), layout.rows * inventoryCellSize(inventory)};
}
inline Rectangle inventoryClose() {
    auto p = inventoryBounds();
    // The 32x32 buysellbtn frame fits the invchar6 socket at (18,385).
    return {p.x + 18 * inventoryScale(), p.y + 385 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle inventoryGold() {
    auto p = inventoryBounds();
    return {p.x + 82 * inventoryScale(), p.y + 391 * inventoryScale(),
            120 * inventoryScale(), 20 * inventoryScale()};
}
inline Rectangle inventoryGoldButton() {
    const auto p = inventoryBounds();
    // Native 1.13c goldcoinbtn anchor, inside the invchar6 gold socket.
    return {p.x + 84 * inventoryScale(), p.y + 392 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}
inline Rectangle inventoryClose(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryClose();
    const auto p = inventoryBounds(inventory);
    return {p.x + 18 * inventoryScale(), p.y + (inventory.panelHeight - 4) * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle inventoryGold(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryGold();
    const auto p = inventoryBounds(inventory);
    // CN invchar6 frames 4-7: the gold socket starts at (72,468),
    // with the extended art's signed DC6 origin at Y=25.
    return {p.x + 72 * inventoryScale(), p.y + (inventory.panelHeight + 1) * inventoryScale(),
            116 * inventoryScale(), 20 * inventoryScale()};
}
inline Rectangle inventoryGoldButton(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return inventoryGoldButton();
    const auto field = inventoryGold(inventory);
    return {field.x + 2 * inventoryScale(), field.y + 2 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}
inline Rectangle inventoryToggle() {
    return hudMenuButton();
}
Rectangle equipmentBounds(const InventoryView &inventory, EquipmentSlot slot);
std::optional<EquipmentSlot> equipmentAt(const InventoryView &inventory, Vec mouse, unsigned weaponSet);
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
inline Rectangle storageBounds(const InventoryView &inventory) {
    auto panel = classicPanelBounds(false, float(inventory.storagePanelTop));
    panel.height = inventory.storagePanelHeight * inventoryScale();
    return panel;
}
// Keep the original CN button art in the strip below the extended MPQ
// panel. Window height includes D2X's HUD and is not a classic panel anchor.
inline Rectangle storageNavigationButton(unsigned button) {
    const auto p = classicPanelBounds(false);
    constexpr std::array x{16, 128, 160};
    return {p.x + x.at(button) * inventoryScale(), classicSideBounds(false).height - 32 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle storageClose() {
    return {storageBounds().x + 274 * inventoryScale(), storageBounds().y + 384 * inventoryScale(),
            31 * inventoryScale(), 34 * inventoryScale()};
}
inline Rectangle storageClose(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageClose();
    const auto p = storageBounds(inventory);
    return {p.x + 274 * inventoryScale(), p.y + (inventory.storagePanelHeight - 14) * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle cubeBounds() {
    return classicPanelBounds(false);
}
inline Rectangle cubeClose() {
    auto p = cubeBounds();
    return {p.x + 272 * inventoryScale(), p.y + 383 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle cubeTransmute() {
    auto p = cubeBounds();
    return {p.x + 109 * inventoryScale(), p.y + 297 * inventoryScale(),
            102 * inventoryScale(), 35 * inventoryScale()};
}
inline Rectangle storageGold(bool expansion) {
    auto p = storageBounds();
    return {p.x + 74 * inventoryScale(),
            p.y + (expansion ? 20 : 218) * inventoryScale(),
            176 * inventoryScale(), 22 * inventoryScale()};
}
inline Rectangle storageGoldButton(bool expansion) {
    const auto p = storageBounds();
    // Native stash button moves down 198 pixels in the classic layout.
    return {p.x + 75 * inventoryScale(),
            p.y + (expansion ? 23 : 221) * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}
inline Rectangle storageGold(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageGold(inventory.stashLayout.expansion);
    const auto p = storageBounds(inventory);
    // CN tradestash's two bottom fields are bank gold and the page name.
    return {p.x + 72 * inventoryScale(), p.y + (inventory.storagePanelHeight - 18) * inventoryScale(),
            176 * inventoryScale(), 20 * inventoryScale()};
}
inline Rectangle storageGoldButton(const InventoryView &inventory) {
    if (!inventory.personalItemActionsOnly) return storageGoldButton(inventory.stashLayout.expansion);
    const auto field = storageGold(inventory);
    return {field.x + 2 * inventoryScale(), field.y + 2 * inventoryScale(),
            20 * inventoryScale(), 18 * inventoryScale()};
}
inline Rectangle goldDialogBounds() {
    // Retail gold menu starts at (215,140) in 640x480, centered when the
    // game canvas grows. The MPQ dialogbackground frame is 210x158.
    return {W / 2.f - 105 * inventoryScale(), (H - HUD) / 2.f - 79 * inventoryScale(),
            210 * inventoryScale(), 158 * inventoryScale()};
}
inline Rectangle goldDialogButton(int index) {
    auto p = goldDialogBounds();
    return {p.x + (35 + index * 105) * inventoryScale(), p.y + 115 * inventoryScale(),
            32 * inventoryScale(), 32 * inventoryScale()};
}
inline Rectangle goldDialogEntry() {
    const auto p = goldDialogBounds();
    return {p.x + 29 * inventoryScale(), p.y + 62 * inventoryScale(),
            152 * inventoryScale(), 24 * inventoryScale()};
}
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
