#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawStorage(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.storage)
        return;
    drawPanelFrame(false);
    if (assets_.storagePanel.frames.size() < 4)
        return; // Never substitute a fabricated stash panel.
    drawPanelTiles(assets_.storagePanel, false);
    if (inventoryView_.personalItemActionsOnly) {
        for (unsigned page = 1; page <= 10; ++page) {
            const unsigned frame = 2 * page + (page == inventoryView_.storagePage);
            if (const auto *tab = assets_.storageTabs.frame(0, int(frame))) {
                auto bounds = storageTab(page);
                DrawTexturePro(tab->texture, {0, 0, float(tab->texture.width), float(tab->texture.height)},
                    bounds, {0, 0}, 0, inventoryView_.storagePagePending || page != inventoryView_.storagePage ? GRAY : WHITE);
            }
        }
    }
    const auto &layout = inventoryView_.stashLayout;
    auto goldField = storageGold(layout.expansion);
    auto bankGold = std::to_string(inventoryView_.bankGold);
    const int textSize = int(16 * inventoryScale());
    if (!inventoryView_.personalItemActionsOnly) {
        if (const auto *coin = assets_.goldCoin.frame(0, 0))
            DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                           storageGoldButton(layout.expansion),
                           {0, 0}, 0, WHITE);
        painter_.label(bankGold,
                       int(goldField.x + (goldField.width - painter_.measure(bankGold, textSize)) / 2),
                       int(goldField.y + 5 * inventoryScale()), textSize, WHITE);
    }
    const auto &inventory = inventoryView_;
    for (const auto &grid : inventoryGrids(inventoryView_, ui)) {
        if (grid.container != ui.storage)
            continue;
        drawContainerGrid(grid, mouse);
        if (!ui.drag)
            if (auto cell = grid.cellAt(mouse))
                if (auto item = inventory.item(inventory.itemAt(grid.container, *cell)))
                    drawItemTooltip(*item, {mouse.x + 170, mouse.y});
    }
    if (!inventoryView_.personalItemActionsOnly) {
        auto maximum = assets_.characterLabels.at("GoldMax");
        if (const auto value = maximum.find("%d"); value != std::string::npos)
            maximum.replace(value, 2, std::to_string(inventoryView_.bankGoldLimit));
        painter_.label(maximum, int(goldField.x + (goldField.width - painter_.measure(maximum, textSize)) / 2),
                       int(goldField.y + 30 * inventoryScale()), textSize, WHITE);
        if (const auto *close = assets_.questClose.frame(0, 10))
            DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                           storageClose(), {0, 0}, 0, WHITE);
    }
}
} // namespace d2x
