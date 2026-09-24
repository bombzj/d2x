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
    const auto &layout = session_.content().stashLayout;
    if (layout.expansion)
        painter_.label("PRIVATE STASH", int(panel.x + 98), int(panel.y + 24 * inventoryScale), 14, gold);
    else
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
    painter_.label("Gold Max: " + std::to_string(session_.bankGoldLimit()),
                   int(panel.x + 108), layout.expansion ? int(panel.y + 53 * inventoryScale) : 326, 13, gold);
    painter_.label(std::to_string(session_.state().player.bankGold),
                   int(storageGold().x + 54), int(storageGold().y + 14), 14, parchment);
    painter_.label("X", int(storageClose().x + 15), int(storageClose().y + 15), 12, gold);
}
} // namespace d2x
