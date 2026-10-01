#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawStorage(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.storage)
        return;
    auto panel = storageBounds();
    drawPanelFrame(false);
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
    auto goldField = storageGold(layout.expansion);
    auto bankGold = std::to_string(session_.state().player.bankGold);
    const int textSize = int(16 * inventoryScale);
    if (const auto *coin = assets_.goldCoin.frame(0, 0))
        DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                       {goldField.x + 3 * inventoryScale, goldField.y + 4 * inventoryScale,
                        coin->texture.width * inventoryScale, coin->texture.height * inventoryScale},
                       {0, 0}, 0, WHITE);
    painter_.label(bankGold,
                   int(goldField.x + (goldField.width - painter_.measure(bankGold, textSize)) / 2),
                   int(goldField.y + 5 * inventoryScale), textSize, WHITE);
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
    const auto maximum = "Gold Max: " + std::to_string(session_.bankGoldLimit());
    painter_.label(maximum, int(goldField.x + (goldField.width - painter_.measure(maximum, textSize)) / 2),
                   int(goldField.y + 30 * inventoryScale), textSize, WHITE);
    if (const auto *close = assets_.questClose.frame(0, 10))
        DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                       storageClose(), {0, 0}, 0, WHITE);
}
} // namespace d2x
