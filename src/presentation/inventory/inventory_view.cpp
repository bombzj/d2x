#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace d2x {
size_t SceneView::goldDialogCaretAt(Vec mouse) const {
    if (!view_.inventory.goldDialog) return 0;
    const auto &amount = view_.inventory.goldDialog->amount;
    float x = goldDialogEntry().x;
    for (size_t index = 0; index < amount.size(); ++index) {
        const float width = assets_.font.widthOf(uint32_t(amount[index])) * inventoryScale();
        if (mouse.x < x + width / 2) return index;
        x += width;
    }
    return amount.size();
}
InventoryDrop SceneView::inventoryDropAt(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.drag) return {};
    const auto *item = inventoryView_.item(ui.drag->item.id);
    if (!item || item->revision != ui.drag->item.revision) return {};
    const auto *icon = assets_.inventoryIcon(item->artKey, item->colorTransform, mapView().palette, item->artFrame);
    if (!icon) return {};
    return inventoryDrop(inventoryView_, inventoryClient_, ui, mouse,
                         {icon->texture.width, icon->texture.height}, view_.hirelingOpen);
}
void SceneView::drawInventoryDrop(const InventoryDrop &drop, Rectangle surface) const {
    const auto &ui = view_.inventory;
    if (characterView_.dead || !ui.drag || !ui.drag->moved || ui.goldDialog || view_.gameMenuOpen ||
        (view_.capturesWorldInput(!ui.playerTradeOpen) && !view_.shopOpen) || drop.bounds.width <= 0)
        return;
    auto bounds = GetCollisionRec(drop.bounds, surface);
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? paletteBlend_.get() : actPaletteBlends_.at(size_t(act)).get();
    if (!blend) return;
    // D2Client 1.13c RVA 0x8C6B0: green (0,128,0), forbidden red (128,0,0).
    // The actual palette index and resulting cell pixels come from the current Act PL2.
    blend->drawRectangle(bounds, drop.error == InventoryError::None ? Color{0, 128, 0, 255}
                                                                   : Color{128, 0, 0, 255});
}
void SceneView::drawItemArt(const std::string &key, Rectangle bounds) const {
    const auto *icon = assets_.inventoryIcon(key, {}, mapView().palette);
    if (icon && icon->texture.id) {
        DrawTexturePro(
            icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)},
            {bounds.x + icon->x * inventoryScale(), bounds.y + icon->y * inventoryScale(),
             icon->texture.width * inventoryScale(), icon->texture.height * inventoryScale()},
            {0, 0}, 0, WHITE);
    }
}
void SceneView::drawItemBackground(const InventoryItemView &item, Rectangle bounds, bool hovered) const {
    if (!hovered && item.cellTone == InventoryCellTone::Unknown) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? paletteBlend_.get() : actPaletteBlends_.at(size_t(act)).get();
    if (!blend) return;
    const Color color = hovered ? Color{0, 128, 0, 255} :
        item.cellTone == InventoryCellTone::Usable ? Color{0, 0, 128, 255} : Color{128, 0, 0, 255};
    blend->drawRectangle(bounds, color);
}
void SceneView::drawItemIcon(const InventoryItemView &item, Rectangle bounds, bool showSockets, float pixelScale) const {
    const float scale = pixelScale > 0 ? pixelScale : inventoryScale();
    const auto [width, height] = item.iconCells;
    if (width < 1 || height < 1) return;
    const auto [cellWidth, cellHeight] = inventoryView_.itemCellSize;
    if (cellWidth < 1 || cellHeight < 1) return;
    const int act = mapView().palette;
    const auto *blend = act == 0 ? paletteBlend_.get() : actPaletteBlends_.at(size_t(act)).get();
    // Equipment centers the occupied grid, rather than shrinking the DC6 to fit.
    const Vec origin{bounds.x + std::floor((bounds.width / scale - width * cellWidth) / 2) * scale,
                     bounds.y + std::floor((bounds.height / scale - height * cellHeight) / 2) * scale};
    auto draw = [&](const std::string &art, std::array<int, 2> transform, uint32_t flags, Vec position, unsigned frame = 0) {
        const auto *icon = assets_.inventoryIcon(art, transform, act, frame);
        if (!icon || !icon->texture.id) return;
        const Rectangle pixels{position.x + icon->x * scale, position.y + icon->y * scale,
            icon->texture.width * scale, icon->texture.height * scale};
        if (flags & 0x400000u) {
            if (blend) blend->drawHalf(icon, pixels);
        } else
            DrawTexturePro(icon->texture, {0, 0, float(icon->texture.width), float(icon->texture.height)}, pixels, {}, 0, WHITE);
    };
    draw(item.artKey, item.colorTransform, item.nativeFlags, origin, item.artFrame);
    if (!showSockets || !item.identified || !item.socketContentsKnown || !(item.nativeFlags & 0x800u) ||
        item.sockets < 1 || item.sockets > 6 || width > 2 || height > 4) return;
    // D2Client 1.13c RVA 95140 / D0A98, coordinates in half-cell units.
    static constexpr int socketPositions[6][8][12]{
        {
            {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 2, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 3, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 0, 0, 0, 0, 0, 0},
            {1, 2, 1, 4, 1, 6, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 1, 2, 3, 0, 0, 0, 0, 0, 0},
            {2, 1, 2, 3, 2, 5, 0, 0, 0, 0, 0, 0},
            {2, 2, 2, 4, 2, 6, 0, 0, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 1, 7, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 3, 3, 1, 3, 3, 1, 0, 0, 0, 0},
            {1, 2, 3, 4, 1, 4, 3, 2, 0, 0, 0, 0},
            {2, 1, 2, 3, 2, 5, 2, 7, 0, 0, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {2, 3, 1, 1, 3, 5, 1, 5, 3, 1, 0, 0},
            {2, 4, 1, 2, 3, 6, 1, 6, 3, 2, 0, 0},
        },
        {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 1, 3, 1, 5, 3, 1, 3, 3, 3, 5},
            {1, 2, 1, 4, 1, 6, 3, 2, 3, 4, 3, 6},
        },
        };
    const auto &positions = socketPositions[item.sockets - 1][(width - 1) * 4 + height - 1];
    const auto *hole = assets_.itemSockets.at(size_t(act)).frame(0, 0);
    for (unsigned index = 0; index < item.sockets; ++index) {
        const int x = positions[index * 2], y = positions[index * 2 + 1];
        if (!x || !y) continue; // No native layout for this footprint/socket count.
        const Vec socket{origin.x + ((x - 1) * cellWidth / 2) * scale,
                         origin.y + ((y + 1) * cellHeight / 2) * scale};
        if (hole && blend)
            blend->drawSocket(hole, {socket.x + (hole->x - 1) * scale,
                socket.y + (hole->y - hole->texture.height) * scale,
                hole->texture.width * scale, hole->texture.height * scale});
        if (index < item.socketItems.size()) {
            const auto &child = item.socketItems[index];
            draw(child.artKey, child.colorTransform, child.nativeFlags,
                 {socket.x, socket.y - cellHeight * scale});
        }
    }
}
void SceneView::drawItemTooltip(const InventoryItemView &item, Vec anchor,
                                std::optional<unsigned> price, std::string_view priceLabel) const {
    drawItemText(item.tooltip, item.quality, anchor, price, priceLabel);
}
void SceneView::drawInventory(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.open)
        return;
    const auto &inventory = inventoryView_;
    EntityId backpack = inventoryView_.containers.backpack;
    auto panel = inventoryBounds(inventory);
    drawPanelFrame(true);
    if (assets_.inventoryPanel.frames.size() >= 8)
        for (int i = 0; i < 4; ++i) {
            const auto &sprite = assets_.inventoryPanel.frames[4 + i];
            const auto &tile = sprite.texture;
            // Native DC6 tiles are bottom-anchored. The unchanged character
            // half supplies the two row baselines; inventory art may be taller
            // and carries its own signed offsets (CN: -35 / +35).
            const auto art = classicPanelBounds(true);
            const auto &upper = assets_.inventoryPanel.frames[0].texture;
            const auto &lower = assets_.inventoryPanel.frames[2].texture;
            const int baseline = upper.height + (i >= 2 ? lower.height : 0);
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {art.x + ((i % 2) * upper.width + sprite.x) * inventoryScale(),
                            art.y + (baseline - tile.height + sprite.y) * inventoryScale(), tile.width * inventoryScale(),
                            tile.height * inventoryScale()},
                           {0, 0}, 0, WHITE);
        }
    auto hoverCell = inventoryCell(inventory, mouse);
    EntityId hovered = hoverCell ? inventory.itemAt(backpack, *hoverCell) : EntityId{};
    const auto weaponSet = inventoryView_.weaponSet;
    const bool overShop = view_.shopOpen && CheckCollisionPointRec(rv(mouse), classicSideBounds(false));
    const auto drop = overShop ? InventoryDrop{} :
        inventoryDropAt(mouse);
    if (!inventory.readOnly && !inventory.personalItemActionsOnly && inventoryView_.stashLayout.expansion && weaponSet == 1)
        for (int hand = 0; hand < 2; ++hand) {
            const auto &tile = assets_.weaponTabs.frames[size_t(hand)].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (hand ? 247.f : 16.f) * inventoryScale(),
                 panel.y + 23 * inventoryScale(), tile.width * inventoryScale(),
                 tile.height * inventoryScale()}, {0, 0}, 0, WHITE);
        }
    for (int index = 0; index < int(EquipmentSlot::AlternateRightHand); ++index) {
        auto slot = EquipmentSlot(index);
        if (slot == EquipmentSlot::RightHand) slot = weaponHandSlot(false, weaponSet);
        if (slot == EquipmentSlot::LeftHand) slot = weaponHandSlot(true, weaponSet);
        auto bounds = equipmentBounds(inventory, slot);
        drawInventoryDrop(drop, bounds);
        auto equipped = inventory.item(inventory.equipped(inventoryView_.containers, slot));
        if (equipped && !ui.hidesItem(*equipped)) {
            const bool over = !ui.drag && CheckCollisionPointRec(rv(mouse), bounds);
            if (over || equipped->cellTone != InventoryCellTone::Usable)
                drawItemBackground(*equipped, bounds, over);
            auto artBounds = bounds;
            // Native body-location adjustments after centering the grid.
            if (slot == EquipmentSlot::Neck || slot == EquipmentSlot::RightRing ||
                slot == EquipmentSlot::LeftRing || slot == EquipmentSlot::Gloves || slot == EquipmentSlot::Belt)
                artBounds.y -= 2 * inventoryScale();
            if (slot == EquipmentSlot::Torso) artBounds.x += 3 * inventoryScale();
            if (slot == EquipmentSlot::Belt || slot == EquipmentSlot::Feet) artBounds.x += 2 * inventoryScale();
            if (slot == EquipmentSlot::Feet) artBounds.y -= 4 * inventoryScale();
            // Original 0x97 handler RVA ABF50 toggles the secondary-set flag;
            // RVA 953CA adds this offset only to its hand slots in expansion.
            if (inventoryView_.stashLayout.expansion && weaponSet &&
                (slot == EquipmentSlot::AlternateRightHand || slot == EquipmentSlot::AlternateLeftHand)) {
                artBounds.x += 4 * inventoryScale();
                artBounds.y -= 4 * inventoryScale();
            }
            drawItemIcon(*equipped, artBounds, over);
        }
        if (CheckCollisionPointRec(rv(mouse), bounds)) {
            if (equipped)
                hovered = equipped->id;
        }
    }
    for (const auto &grid : inventoryGrids(inventoryView_, ui))
        if (grid.container == backpack)
            drawContainerGrid(grid, mouse);
    if (!inventory.readOnly && !inventory.personalItemActionsOnly) {
        const auto goldField = inventoryGold();
        if (const auto *coin = assets_.goldCoin.frame(0, 0))
            DrawTexturePro(coin->texture, {0, 0, float(coin->texture.width), float(coin->texture.height)},
                           inventoryGoldButton(),
                           {0, 0}, 0, WHITE);
        if (const auto *close = assets_.questClose.frame(0, 10))
            DrawTexturePro(close->texture, {0, 0, float(close->texture.width), float(close->texture.height)},
                           inventoryClose(), {0, 0}, 0, WHITE);
        painter_.label(std::to_string(inventoryView_.gold),
                       int(goldField.x + 28 * inventoryScale()), int(goldField.y + 4 * inventoryScale()),
                       int(16 * inventoryScale()), WHITE);
    }
    if (ui.identify)
        painter_.label("SELECT AN UNIDENTIFIED ITEM", int(panel.x + 24 * inventoryScale()),
                       int(panel.y + 364 * inventoryScale()), 12, gold);
    if (!ui.drag) {
        if (auto item = inventory.item(hovered); item && !ui.hidesItem(*item))
            drawItemTooltip(*item, {mouse.x + 170, mouse.y},
                inventoryVendorPrice(item->handle()), view_.shopOpen ? (view_.shopRepair ? "COST" : "SELL VALUE") : "");
    }
    if (ui.goldDialog) {
        const auto dialog = goldDialogBounds();
        const auto draw = [](const Sprite *image, Rectangle bounds) {
            if (image) DrawTexturePro(image->texture,
                {0, 0, float(image->texture.width), float(image->texture.height)}, bounds, {}, 0, WHITE);
        };
        draw(assets_.inventoryGoldDialog.frame(0, 0), dialog);
        const auto measure = [](const ClassicFont &font, const std::string &text) {
            float width = 0;
            for (auto character : utf8Decode(text)) width += font.widthOf(character);
            return width;
        };
        const auto label = [&](const ClassicFont &font, const std::string &text, float x, float baseline) {
            for (auto character : utf8Decode(text)) {
                const auto *glyph = font.glyphOf(character);
                if (glyph && character != ' ')
                    draw(glyph, {dialog.x + (x + glyph->x) * inventoryScale(),
                        dialog.y + (baseline + glyph->y - glyph->texture.height) * inventoryScale(),
                        glyph->texture.width * inventoryScale(), glyph->texture.height * inventoryScale()});
                x += font.widthOf(character);
            }
        };
        const char *key = ui.goldDialog->action == GoldAction::Deposit ? "strBankGoldDeposit" :
            ui.goldDialog->action == GoldAction::Withdraw ? "strBankGoldWithdraw" : "strDropGoldHowMuch";
        const auto &questionFont = assets_.itemTextFonts[4];
        std::istringstream words(assets_.characterLabels.at(key));
        std::string line, word;
        float baseline = 24;
        const auto flush = [&] {
            label(questionFont, line, (210 - measure(questionFont, line)) / 2, baseline);
            baseline += 18;
        };
        while (words >> word) {
            const auto candidate = line.empty() ? word : line + ' ' + word;
            if (!line.empty() && measure(questionFont, candidate) > 194) { flush(); line = word; }
            else line = candidate;
        }
        if (!line.empty()) flush();
        const auto &entry = *ui.goldDialog;
        const auto prefixWidth = [&](size_t count) {
            return measure(assets_.font,entry.amount.substr(0,count));
        };
        const auto first = std::min(entry.caret,entry.anchor), last = std::max(entry.caret,entry.anchor);
        if (first != last) {
            // D2Win EditBox draws the selected text over MixRGB(0x40,0x40,0x40).
            DrawRectangleRec({dialog.x + (29 + prefixWidth(first)) * inventoryScale(),
                dialog.y + 66 * inventoryScale(), (prefixWidth(last)-prefixWidth(first)) * inventoryScale(),
                16 * inventoryScale()}, {64,64,64,255});
        }
        label(assets_.font, entry.amount, 29, 82);
        if (uint64_t(GetTime() * 4) & 1)
            label(assets_.font, "_", 29 + prefixWidth(entry.caret), 82);
        draw(assets_.questClose.frame(0, 16), goldDialogButton(0));
        draw(assets_.questClose.frame(0, 10), goldDialogButton(1));
    }
}
bool SceneView::drawInventoryCursor(Vec mouse) const {
    if (characterView_.dead) return false;
    const auto &ui = view_.inventory;
    if (!ui.drag || view_.gameMenuOpen ||
        (view_.capturesWorldInput(!ui.playerTradeOpen) && !view_.shopOpen))
        return false;
    const auto *item = inventoryView_.item(ui.drag->item.id);
    if (!item || item->revision != ui.drag->item.revision)
        return false;
    const auto &definition = *inventoryView_.definition(item->definition);
    const auto *icon = assets_.inventoryIcon(item->artKey, item->colorTransform, mapView().palette, item->artFrame);
    if (!icon) return true;
    // Native cursor RVA 169B8 centers the DC6 dimensions, not its grid footprint.
    drawItemIcon(*item,
                 {mouse.x - (icon->texture.width / 2) * inventoryScale(),
                  mouse.y - (icon->texture.height / 2) * inventoryScale(),
                  definition.width * inventoryCellSize(inventoryView_), definition.height * inventoryCellSize(inventoryView_)});
    return true;
}
} // namespace d2x
