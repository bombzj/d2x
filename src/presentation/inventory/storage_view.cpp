#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawStorage(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.storage)
        return;
    drawPanelFrame(false);
    if (assets_.storagePanel.frames.size() < 4)
        return; // Never substitute a fabricated stash panel.
    const bool materials = inventoryView_.storageMaterialsOpen && storageMaterialsReady();
    drawPanelTiles(assets_.storagePanel, false, materials ? 4 : 0);
    if (inventoryView_.personalItemActionsOnly && storageNavigationReady()) {
        const bool ready = inventoryView_.storagePagingAvailable && !inventoryView_.storagePagePending && !inventoryView_.itemOperationPending;
        constexpr std::array frames{4u, 8u, 0u, 2u, 10u};
        constexpr std::array labels{"Personal / shared stash", "Previous index (Shift: main index)",
            "Previous page (Shift: first)", "Next page (Shift: last nonempty)",
            "Next index (Shift: main index)", "Shared materials (Ctrl when open: deposit backpack materials)"};
        for (unsigned button = 0; button < 6; ++button) {
            if (button == 5 && !storageMaterialsReady()) continue;
            unsigned frame = button == 5 ? (materials ? 30 : 28) :
                (button == 0 && inventoryView_.storageShared ? 6 : frames.at(button));
            const bool available = button == 5 ? inventoryView_.storageMaterialsAvailable && !inventoryView_.storagePagePending && !inventoryView_.itemOperationPending :
                ready && (button == 0 || (inventoryView_.storageCnConfirmed &&
                    ((button != 1 && button != 2) || inventoryView_.storagePage > 1)));
            if (available && ui.storagePressedButton == button) ++frame;
            const auto *image = button == 5 ? assets_.vendorButtons.frame(0, int(frame)) :
                assets_.storageNavigation.frame(0, int(frame));
            const auto bounds = storageNavigationButton(button, inventoryView_);
            DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                           bounds, {0, 0}, 0, available ? WHITE : GRAY);
            if (CheckCollisionPointRec(rv(mouse), bounds))
                drawItemText({{labels.at(button)}}, ItemQuality::Normal, {mouse.x + 160, mouse.y}, {}, {});
        }
    }
    auto goldField = storageGold(inventoryView_);
    const bool sharedGold = inventoryView_.personalItemActionsOnly &&
        (materials || (inventoryView_.storageCnConfirmed && inventoryView_.storageShared));
    const bool balanceKnown = sharedGold ? inventoryView_.storageSharedGold.has_value() : inventoryView_.bankGoldKnown;
    auto bankGold = std::to_string(sharedGold ? inventoryView_.storageSharedGold.value_or(0) : inventoryView_.bankGold);
    const int textSize = int(16 * inventoryScale());
    if (!inventoryView_.personalItemActionsOnly ||
        inventoryView_.storageCnConfirmed || materials) {
        if (const auto *coin = assets_.goldCoin.frame(0, 0))
            DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                           storageGoldButton(inventoryView_),
                           {0, 0}, 0, !inventoryView_.readOnly && (!inventoryView_.personalItemActionsOnly ||
                               (!sharedGold && inventoryView_.goldKnown && inventoryView_.bankGoldKnown &&
                                inventoryView_.storagePagingAvailable && !inventoryView_.storagePagePending)) ? WHITE : GRAY);
        if (!inventoryView_.personalItemActionsOnly || balanceKnown)
            painter_.label(bankGold,
                           int(goldField.x + (goldField.width - painter_.measure(bankGold, textSize)) / 2),
                           int(goldField.y + 5 * inventoryScale()), textSize, WHITE);
    }
    const auto &inventory = inventoryView_;
    if (materials) for (const auto &material : inventory.storageMaterials) {
        const auto bounds = storageMaterialBounds(material);
        const auto *icon = assets_.inventoryIcon(material.artKey, {}, mapView().palette);
        if (icon && icon->texture.id) {
            const float scale = inventoryScale();
            DrawTexturePro(icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)},
                {bounds.x + icon->x * scale, bounds.y + icon->y * scale,
                 icon->texture.width * scale, icon->texture.height * scale}, {}, 0,
                material.quantity ? WHITE : Color{70,70,70,255});
        }
        if (!material.upgradeSource.empty() && storageMaterialUpgradeReady()) {
            const auto hit = storageMaterialUpgradeBounds(material);
            const auto *arrow = assets_.storageMaterialUpgrade.frame(0, 0);
            const float scale = inventoryScale();
            DrawTexturePro(arrow->texture, {0, 0, float(arrow->texture.width), float(arrow->texture.height)},
                {hit.x - (material.upgradeOnLeft ? 5 : 0) * scale,
                 bounds.y + (material.height * 29 + 1 - arrow->texture.height) * scale,
                 arrow->texture.width * scale, arrow->texture.height * scale}, {}, 0,
                material.upgradeQuantity >= 3 ? WHITE : GRAY);
            if (CheckCollisionPointRec(rv(mouse), hit))
                drawItemText({{"Convert 3:1 (Ctrl: 15; Shift: 150)"}}, ItemQuality::Normal, {mouse.x + 170, mouse.y}, {}, {});
        }
        if (material.quantity) {
            const auto quantity = material.quantity < 10000 ? std::to_string(material.quantity) :
                std::to_string(std::min(99u, material.quantity / 1000)) + "k+";
            painter_.label(quantity, int(bounds.x), int(bounds.y + bounds.height - 12 * inventoryScale()),
                           int(12 * inventoryScale()), WHITE);
        }
        if (CheckCollisionPointRec(rv(mouse), bounds))
            drawItemText({{material.name}, {"Quantity: " + std::to_string(material.quantity)},
                {"Left: cursor; right: auto store"}, {"Ctrl+right: 5; Shift+right: 50; Ctrl+Shift: cube"}},
                ItemQuality::Normal, {mouse.x + 170, mouse.y}, {}, {});
    }
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
    if (inventoryView_.personalItemActionsOnly) {
        const auto name = materials ? std::string("Shared materials") :
            !inventoryView_.storagePageName.empty() ? inventoryView_.storagePageName :
            std::string(inventoryView_.storageShared ? "Shared stash: " : "Personal stash: ") +
                (inventoryView_.storageCnConfirmed ? std::to_string(inventoryView_.storagePage) : "?");
        painter_.label(name,
                       int(goldField.x + 4 * inventoryScale()), int(goldField.y + 21 * inventoryScale()),
                       textSize, WHITE);
        if (!materials && inventoryView_.storageCnConfirmed && CheckCollisionPointRec(rv(mouse), storageNameField(inventoryView_)))
            drawItemText({{inventoryView_.storageIndex == 2 ? "Main index" : inventoryView_.storageIndex == 1 ? "Index" : "Normal page"},
                {"Click: cycle page index"}}, ItemQuality::Normal, {mouse.x + 170, mouse.y}, {}, {});
    }
    if (const auto *close = assets_.questClose.frame(0, 10))
        DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                       storageClose(inventoryView_), {0, 0}, 0, WHITE);
}
} // namespace d2x
