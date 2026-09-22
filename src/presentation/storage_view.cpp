#include "scene_view.hpp"

namespace d2x {
void SceneView::drawStorage(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.storage)
        return;
    auto panel = storageBounds();
    DrawRectangle(0, 0, int(panel.x + panel.width + 6), H - HUD, {0, 0, 0, 150});
    if (assets_.storagePanel.frames.size() < 4)
        return; // Never substitute a fabricated stash panel.
    for (int i = 0; i < 4; ++i) {
        auto tile = assets_.storagePanel.frames[i].texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                       {panel.x + (i % 2) * 256 * inventoryScale, panel.y + (i / 2) * 256 * inventoryScale,
                        tile.width * inventoryScale, tile.height * inventoryScale},
                       {0, 0}, 0, WHITE);
    }
    painter_.label("PRIVATE STASH", int(panel.x + 94), 302, 16, gold);
    const auto &inventory = session_.inventory();
    for (const auto &grid : inventoryGrids(session_, ui)) {
        if (grid.container != ui.storage)
            continue;
        drawContainerGrid(grid, mouse);
        if (!ui.drag && !ui.split)
            if (auto cell = grid.cellAt(mouse))
                if (auto item = inventory.item(inventory.itemAt(grid.container, *cell)))
                    drawItemTooltip(*item, {inventoryBounds().x - 12, mouse.y});
    }
    int used = 0;
    for (auto id : inventory.contents(ui.storage)) {
        auto def = inventory.catalog().find(inventory.item(id)->definition);
        used += def->width * def->height;
    }
    painter_.label(std::to_string(used) + " / 24", int(panel.x + 160), 326, 12, gold);
    itemButton(storageTransfer(), "TRANSFER SELECTED", inventory.item(ui.selected) ? gold : GRAY);
    painter_.label("X", int(storageClose().x + 15), int(storageClose().y + 15), 12, gold);
    painter_.label("Shift-click: transfer   I / Esc: close", int(panel.x + 60), 544, 10, gold);
}
} // namespace d2x
