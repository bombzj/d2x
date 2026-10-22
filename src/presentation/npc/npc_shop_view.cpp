#include "presentation/scene_view.hpp"
#include <algorithm>
#include <array>

namespace d2x {
std::optional<unsigned> SceneView::inventoryVendorPrice(ItemHandle item) const {
    return (view_.shopRepair || view_.shopOpen)
        ? npcClient_.quote(view_.dialogueObject, item, view_.shopRepair) : std::nullopt;
}

namespace {
float scale() { return inventoryScale(); }
Rectangle panelBounds() {
    return storageBounds();
}
Rectangle shopGrid(const ShopView &shop, float cellSize) {
    auto panel = panelBounds();
    return {panel.x + 14 * scale(), panel.y + shop.gridTop * scale(),
            10 * cellSize, 10 * cellSize};
}
Rectangle tabBounds(int index) {
    auto panel = panelBounds();
    return {panel.x + index * 80 * scale(), panel.y, 80 * scale(), 28 * scale()};
}
Rectangle shopButton(int index) {
    auto panel = panelBounds();
    return {panel.x + (115 + index * 52) * scale(), panel.y + 384 * scale(),
            32 * scale(), 32 * scale()};
}
Rectangle backButton() { return shopButton(3); }
Rectangle pageButton(bool next) {
    auto panel = panelBounds();
    return {panel.x + (next ? 61.f : 14.f) * scale(), panel.y + 384 * scale(), 38 * scale(), 32 * scale()};
}
struct Confirmation {
    std::string title;
    std::vector<std::string> rows;
};
Confirmation confirmation(const ShopView &shop, uint32_t slot,
    const std::map<std::string,std::string,std::less<>> &labels) {
    Confirmation menu{labels.at("VerifyTransaction5"), {}};
    const auto found = std::find_if(shop.offers.begin(),shop.offers.end(),
        [&](const ShopOfferView &offer) { return offer.slot == slot; });
    if (found == shop.offers.end()) return menu;
    menu.rows = {found->name.substr(0,found->name.find('\n')),labels.at("VerifyTransaction3"),
        found->priceKnown ? std::to_string(found->price) : std::string{},
        labels.at("VerifyTransaction1"),labels.at("VerifyTransaction2")};
    return menu;
}
Rectangle confirmationBounds(const OriginalMenu &painter, Vec anchor, const Confirmation &menu) {
    return painter.bounds(anchor,{0,0,float(W),float(H)},menu.title,menu.rows);
}
struct Placement { const ShopOfferView *offer; int page; Rectangle bounds; };
std::vector<Placement> placements(const ShopView &shop, int category, float cellSize) {
    std::vector<Placement> result;
    if (!shop.available) return result;
    std::array<bool, 100> occupied{};
    int page = 0;
    auto grid = shopGrid(shop, cellSize);
    for (const auto &offer : shop.offers) {
        const auto *item = &offer;
        if (!shop.gamble && (category==1 || category==2 ? offer.storePage!=1 && offer.storePage!=2 : offer.storePage!=category))
            continue;
        if (item->width < 1 || item->height < 1 || item->width > 10 || item->height > 10)
            continue;
        if(offer.cell) {
            const auto [x,y]=*offer.cell;
            if(x<0 || y<0 || x+item->width>10 || y+item->height>10) continue;
            result.push_back({&offer,!shop.gamble && offer.storePage==2?1:0,
                {grid.x+x*cellSize,grid.y+y*cellSize,
                 item->width*cellSize,item->height*cellSize}});
            continue;
        }
        auto locate = [&]() -> std::optional<std::array<int, 2>> {
            for (int y = 0; y <= 10 - item->height; ++y)
                for (int x = 0; x <= 10 - item->width; ++x) {
                    bool free = true;
                    for (int yy = y; yy < y + item->height; ++yy)
                        for (int xx = x; xx < x + item->width; ++xx)
                            free &= !occupied[size_t(yy * 10 + xx)];
                    if (free) return std::array{x, y};
                }
            return {};
        };
        auto cell = locate();
        if (!cell) {
            ++page;
            occupied.fill(false);
            cell = locate();
        }
        if (!cell) continue;
        for (int y = (*cell)[1]; y < (*cell)[1] + item->height; ++y)
            for (int x = (*cell)[0]; x < (*cell)[0] + item->width; ++x)
                occupied[size_t(y * 10 + x)] = true;
        result.push_back({&offer, page,
                          {grid.x + (*cell)[0] * cellSize,
                           grid.y + (*cell)[1] * cellSize,
                           item->width * cellSize,
                           item->height * cellSize}});
    }
    return result;
}
int maximumPage(const std::vector<Placement> &items) {
    int maximum=0;for(const auto &item:items) maximum=std::max(maximum,item.page);return maximum;
}
bool tabAvailable(const ShopView &shop, int tab, float cellSize) {
    auto items = placements(shop, tab, cellSize);
    return tab == 2 ? maximumPage(items) > 0 : !items.empty();
}
} // namespace
bool SceneView::openNpcShop(bool gamble) {
    if (!view_.npcMenu || !npcClient_.shop(view_.dialogueObject, gamble).available) return false;
    view_.npcMenu = false;
    view_.shopOpen = true;
    view_.shopGamble = gamble;
    view_.shopRepair = view_.shopSell = false;
    view_.shopPage = 0;
    view_.shopConfirm.reset();
    view_.shopSalePending.reset();
    view_.inventory.cancelGesture();
    view_.inventory.open = true;
    view_.inventory.cubeOpen = false;
    view_.partyOpen = view_.questOpen = view_.characterOpen = view_.skillTreeOpen = view_.hirelingOpen = false;
    // Native 1.13c RVA 9C213 opens trade on page 3. Shelf packets arrive
    // asynchronously; their partial contents cannot choose the initial page.
    view_.shopCategory = gamble ? 0 : 3;
    return true;
}
void SceneView::closeNpcShop() {
    view_.shopOpen = false;
    view_.shopRepair = view_.shopSell = false;
    view_.npcMenu = false;
    view_.shopConfirm.reset();
    view_.shopSalePending.reset();
    view_.inventory.cancelGesture();
    view_.inventory.open = false;
}
bool SceneView::npcShopDropAt(Vec mouse) const {
    return view_.shopOpen && CheckCollisionPointRec(rv(mouse), shopGrid(shopView(), inventoryCellSize(inventoryView_)));
}
bool SceneView::npcShopRepairAllAt(Vec mouse) const {
    return view_.shopOpen && !view_.shopGamble && !view_.shopConfirm && !shopView().sellModeAvailable && shopView().repairAvailable &&
        assets_.vendorButtons.count > 18 &&
        CheckCollisionPointRec(rv(mouse), shopButton(1));
}
void SceneView::scrollNpcShop(int pages) {
    auto items = placements(shopView(), view_.shopCategory, inventoryCellSize(inventoryView_));
    view_.shopPage = std::clamp(view_.shopPage + pages, 0, maximumPage(items));
    if (view_.shopCategory == 1 || view_.shopCategory == 2)
        view_.shopCategory = view_.shopPage == 0 ? 1 : 2;
    view_.shopConfirm.reset();
}
std::optional<uint32_t> SceneView::clickNpcShop(Vec mouse, bool directBuy) {
    if (view_.shopConfirm) {
        if (directBuy) {
            view_.shopConfirm.reset();
            return {};
        }
        const auto menu = confirmation(shopView(),*view_.shopConfirm,assets_.characterLabels);
        const auto box = confirmationBounds(originalMenu_,view_.shopConfirmAt,menu);
        const int selected = originalMenu_.hit(box,true,menu.rows.size(),mouse);
        if (selected == 3) {
            auto slot = *view_.shopConfirm;
            view_.shopConfirm.reset();
            return slot;
        }
        if (selected == 4)
            view_.shopConfirm.reset();
        return {};
    }
    if (directBuy) {
        for (const auto &item : placements(shopView(), view_.shopCategory, inventoryCellSize(inventoryView_)))
            if (item.page == view_.shopPage && CheckCollisionPointRec(rv(mouse), item.bounds))
                return item.offer->slot;
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), backButton())) {
        closeNpcShop();
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), shopButton(0))) {
        view_.shopRepair = view_.shopSell = false;
        return {};
    }
    if (!view_.shopGamble && shopView().sellModeAvailable && CheckCollisionPointRec(rv(mouse), shopButton(1))) {
        view_.shopSell = !view_.shopSell; view_.shopRepair = false;
        return {};
    }
    if (!view_.shopGamble && CheckCollisionPointRec(rv(mouse), shopButton(2))) {
        if (shopView().repairAvailable) { view_.shopRepair = !view_.shopRepair; view_.shopSell = false; }
        return {};
    }
    for (int index = 0; !view_.shopGamble && index < 4; ++index)
        if (CheckCollisionPointRec(rv(mouse), tabBounds(index))) {
            if (!tabAvailable(shopView(), index, inventoryCellSize(inventoryView_))) return {};
            view_.shopCategory = index;
            view_.shopPage = index == 2 ? 1 : 0;
            return {};
        }
    if (CheckCollisionPointRec(rv(mouse), pageButton(false))) {
        scrollNpcShop(-1);
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), pageButton(true))) {
        scrollNpcShop(1);
        return {};
    }
    for (const auto &item : placements(shopView(), view_.shopCategory, inventoryCellSize(inventoryView_)))
        if (item.page == view_.shopPage &&
            CheckCollisionPointRec(rv(mouse), item.bounds)) {
            view_.shopConfirm = item.offer->slot;
            view_.shopConfirmAt = mouse;
            view_.shopConfirmChoice = 0;
            return {};
        }
    return {};
}
void SceneView::drawNpcShop(Vec mouse) const {
    auto panel = panelBounds();
    drawPanelFrame(false);
    if (assets_.vendorPanel.frames.size() >= 4)
        for (int index = 0; index < 4; ++index) {
            const auto &tile = assets_.vendorPanel.frames[size_t(index)].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {panel.x + (index % 2) * 256 * scale(),
                            panel.y + (index / 2) * 256 * scale(),
                            tile.width * scale(), tile.height * scale()}, {0, 0}, 0, WHITE);
        }
    const auto &shop = shopView();
    for (int index = 0; !view_.shopGamble && index < 4; ++index) {
        auto tab = tabBounds(index);
        const bool available = tabAvailable(shopView(), index, inventoryCellSize(inventoryView_));
        if (!available) continue;
        bool selected = index == view_.shopCategory;
        if (auto sprite = assets_.vendorTabs.frame(0, selected ? index : index + 4)) {
            const auto &texture = sprite->texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           tab, {0, 0}, 0, WHITE);
        }
        const auto &label = shop.tabLabels[size_t(index)];
        painter_.label(label, int(tab.x + (tab.width - painter_.measure(label, 13)) / 2),
                       int(tab.y) + 9, 13, gold);
    }
    auto items = placements(shopView(), view_.shopCategory, inventoryCellSize(inventoryView_));
    const ShopOfferView *hovered = nullptr;
    for (const auto &entry : items) {
        if (entry.page != view_.shopPage) continue;
        const auto *item = inventoryView_.item(EntityId{uint64_t(entry.offer->slot) + 1});
        const bool over = !view_.inventory.drag && CheckCollisionPointRec(rv(mouse), entry.bounds);
        if (item) {
            drawItemBackground(*item, entry.bounds, over);
            drawItemIcon(*item, entry.bounds, over);
        }
        else drawItemArt(entry.offer->artKey, entry.bounds);
        if (CheckCollisionPointRec(rv(mouse), entry.bounds)) {
            hovered = entry.offer;
        }
    }
    const std::string stashGold = std::to_string(shop.bankGold);
    painter_.label(assets_.characterLabels.at("stash"), int(panel.x + 20 * scale()), int(panel.y + 362 * scale()), 13, WHITE);
    painter_.label(stashGold, int(panel.x + 199 * scale()) - painter_.measure(stashGold, 13),
                   int(panel.y + 362 * scale()), 13, WHITE);
    const int buttonFrames[] = {2, shop.sellModeAvailable ? 4 : 18, 6, 10};
    const bool repairAvailable = shop.repairAvailable;
    for (int index = 0; index < 4; ++index) {
        if ((index == 1 && !shop.sellModeAvailable && !repairAvailable) || (index == 2 && !repairAvailable)) continue;
        if (index == 1 && !shop.sellModeAvailable && assets_.vendorButtons.count <= 18) continue;
        auto sprite = assets_.vendorButtons.frame(0, buttonFrames[index]);
        if (!sprite) continue;
        const auto &texture = sprite->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                       shopButton(index), {0, 0}, 0,
                       WHITE);
        if ((index == 2 && view_.shopRepair) || (index == 1 && view_.shopSell)) DrawRectangleLinesEx(shopButton(index), 1, gold);
        if (index == 2 && CheckCollisionPointRec(rv(mouse), shopButton(index)))
            painter_.label(assets_.characterLabels.at("NPCRepairItems"), int(shopButton(index).x), int(shopButton(index).y - 20), 12, WHITE);
        if (index == 1 && !shop.sellModeAvailable && CheckCollisionPointRec(rv(mouse), shopButton(index)))
            deferTooltip({{"Repair All",ItemTextTone::Normal}}, ItemQuality::Normal, {panel.x + panel.width, panel.y + 384 * scale()}, shop.repairAllPrice, assets_.characterLabels.at("Cost"));
    }
    if (maximumPage(items) > (view_.shopCategory == 1 || view_.shopCategory == 2 ? 1 : 0)) {
        painter_.label("<", int(pageButton(false).x + 14), int(pageButton(false).y + 8), 15, gold);
        painter_.label(">", int(pageButton(true).x + 14), int(pageButton(true).y + 8), 15, gold);
        painter_.label(std::to_string(view_.shopPage + 1) + "/" +
                       std::to_string(maximumPage(items) + 1),
                       int(panel.x + 36 * scale()), int(panel.y + 416 * scale()), 11, gold);
    }
    if (hovered && !view_.shopConfirm && !view_.inventory.drag) {
        if (const auto *detail = npcClient_.inspectShopOffer(view_.dialogueObject, hovered->slot, view_.shopGamble))
            deferTooltip(detail->tooltip, detail->quality, {mouse.x + 170, mouse.y}, detail->priceKnown ? std::optional<unsigned>{detail->price} : std::nullopt, assets_.characterLabels.at("Cost"));
    }
    if (view_.shopConfirm) {
        const auto menu = confirmation(shop,*view_.shopConfirm,assets_.characterLabels);
        const auto box = confirmationBounds(originalMenu_,view_.shopConfirmAt,menu);
        const int hovered = originalMenu_.hit(box,true,menu.rows.size(),mouse);
        originalMenu_.draw(box,menu.title,menu.rows,hovered >= 3 ? hovered : 3+view_.shopConfirmChoice);
    }
}
} // namespace d2x
