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
    if (inventoryView_.personalItemActionsOnly && storageNavigationReady()) {
        const bool ready = inventoryView_.storagePagingAvailable && !inventoryView_.storagePagePending;
        for (unsigned button = 0; button < 3; ++button) {
            const unsigned frame = button == 0 ? (inventoryView_.storageShared ? 6 : 4) : (button == 1 ? 0 : 2);
            const bool available = ready && (button == 0 || (inventoryView_.storageCnConfirmed &&
                (button != 1 || inventoryView_.storagePage > 1)));
            const auto *image = assets_.storageNavigation.frame(0, int(frame));
            const auto bounds = storageNavigationButton(button);
            DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                           bounds, {0, 0}, 0, available ? WHITE : GRAY);
        }
        const auto toggle = storageNavigationButton(0);
        const auto page = inventoryView_.storageCnConfirmed ? std::to_string(inventoryView_.storagePage) : "?";
        painter_.label(page, int(toggle.x + 48 * inventoryScale()),
                       int(toggle.y + 8 * inventoryScale()), int(16 * inventoryScale()), ready ? WHITE : GRAY);
    }
    auto goldField = storageGold(inventoryView_);
    auto bankGold = std::to_string(inventoryView_.bankGold);
    const int textSize = int(16 * inventoryScale());
    if (!inventoryView_.personalItemActionsOnly || (inventoryView_.storageCnConfirmed && !inventoryView_.storageShared)) {
        if (const auto *coin = assets_.goldCoin.frame(0, 0))
            DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                           storageGoldButton(inventoryView_),
                           {0, 0}, 0, inventoryView_.personalItemActionsOnly ? GRAY : WHITE);
        if (!inventoryView_.personalItemActionsOnly || inventoryView_.bankGoldKnown)
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
    }
    if (inventoryView_.personalItemActionsOnly && inventoryView_.storageCnConfirmed &&
        !inventoryView_.storagePagePending && !inventoryView_.storagePageName.empty())
        painter_.label(inventoryView_.storagePageName,
                       int(goldField.x + 4 * inventoryScale()), int(goldField.y + 21 * inventoryScale()),
                       textSize, WHITE);
    if (const auto *close = assets_.questClose.frame(0, 10))
        DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                       storageClose(inventoryView_), {0, 0}, 0, WHITE);
}
} // namespace d2x
