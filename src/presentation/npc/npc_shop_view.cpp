#include "presentation/scene_view.hpp"
#include <algorithm>
#include <array>

namespace d2x {
std::optional<unsigned> SceneView::inventoryVendorPrice(ItemHandle item) const {
    return view_.shopRepair || view_.shopOpen
        ? npcClient_.quote(view_.dialogueObject, item, view_.shopRepair) : std::nullopt;
}

namespace {
constexpr float scale = inventoryScale;
Rectangle panelBounds() {
    return storageBounds();
}
Rectangle shopGrid() {
    auto panel = panelBounds();
    return {panel.x + 14 * scale, panel.y + 62 * scale,
            10 * inventoryCellSize, 10 * inventoryCellSize};
}
Rectangle tabBounds(int index) {
    auto panel = panelBounds();
    return {panel.x + index * 80 * scale, panel.y, 80 * scale, 28 * scale};
}
Rectangle shopButton(int index) {
    auto panel = panelBounds();
    return {panel.x + (115 + index * 52) * scale, panel.y + 384 * scale,
            32 * scale, 32 * scale};
}
Rectangle backButton() { return shopButton(3); }
Rectangle pageButton(bool next) {
    auto panel = panelBounds();
    return {panel.x + (next ? 61.f : 14.f) * scale, panel.y + 384 * scale, 38 * scale, 32 * scale};
}
Rectangle confirmBounds() {
    auto panel = panelBounds();
    return {panel.x + 73, panel.y + 158, 265, 248};
}
Rectangle confirmButton(bool yes) {
    auto bounds = confirmBounds();
    return {bounds.x + (yes ? 43.f : 174.f), bounds.y + 184, 45, 46};
}
struct Placement { const ShopOfferView *offer; int page; Rectangle bounds; };
std::vector<Placement> placements(const ShopView &shop, int category) {
    std::vector<Placement> result;
    if (!shop.available) return result;
    std::array<bool, 100> occupied{};
    int page = 0;
    auto grid = shopGrid();
    for (const auto &offer : shop.offers) {
        const auto *item = &offer;
        if (!shop.gamble && offer.storePage != (category == 2 ? 1 : category))
            continue;
        if (item->width < 1 || item->height < 1 || item->width > 10 || item->height > 10)
            continue;
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
                          {grid.x + (*cell)[0] * inventoryCellSize,
                           grid.y + (*cell)[1] * inventoryCellSize,
                           item->width * inventoryCellSize,
                           item->height * inventoryCellSize}});
    }
    return result;
}
int maximumPage(const std::vector<Placement> &items) {
    return items.empty() ? 0 : items.back().page;
}
bool tabAvailable(const ShopView &shop, int tab) {
    auto items = placements(shop, tab);
    return tab == 2 ? maximumPage(items) > 0 : !items.empty();
}
} // namespace
bool SceneView::openNpcShop(bool gamble) {
    if (!view_.npcMenu || !npcClient_.shop(view_.dialogueObject, gamble).available) return false;
    view_.npcMenu = false;
    view_.shopOpen = true;
    view_.shopGamble = gamble;
    view_.shopRepair = false;
    view_.shopPage = 0;
    view_.shopConfirm.reset();
    view_.shopSalePending.reset();
    view_.inventory.cancelGesture();
    view_.inventory.open = true;
    view_.inventory.cubeOpen = false;
    view_.questOpen = view_.characterOpen = view_.skillTreeOpen = view_.hirelingOpen = false;
    view_.shopCategory = 0;
    for (int tab = 0; tab < 4; ++tab)
        if (!gamble && tabAvailable(shopView(), tab)) {
            view_.shopCategory = tab;
            view_.shopPage = tab == 2 ? 1 : 0;
            break;
        }
    return true;
}
void SceneView::closeNpcShop() {
    view_.shopOpen = false;
    view_.shopRepair = false;
    view_.npcMenu = false;
    view_.shopConfirm.reset();
    view_.shopSalePending.reset();
    view_.inventory.cancelGesture();
    view_.inventory.open = false;
}
bool SceneView::npcShopDropAt(Vec mouse) const {
    return view_.shopOpen && CheckCollisionPointRec(rv(mouse), shopGrid());
}
void SceneView::scrollNpcShop(int pages) {
    auto items = placements(shopView(), view_.shopCategory);
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
        if (CheckCollisionPointRec(rv(mouse), confirmButton(true))) {
            auto slot = *view_.shopConfirm;
            view_.shopConfirm.reset();
            return slot;
        }
        if (CheckCollisionPointRec(rv(mouse), confirmButton(false)))
            view_.shopConfirm.reset();
        return {};
    }
    if (directBuy) {
        for (const auto &item : placements(shopView(), view_.shopCategory))
            if (item.page == view_.shopPage && CheckCollisionPointRec(rv(mouse), item.bounds))
                return item.offer->slot;
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), backButton())) {
        closeNpcShop();
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), shopButton(0))) {
        view_.shopRepair = false;
        return {};
    }
    if (!view_.shopGamble && CheckCollisionPointRec(rv(mouse), shopButton(2))) {
        if (shopView().repairAvailable) view_.shopRepair = !view_.shopRepair;
        return {};
    }
    for (int index = 0; !view_.shopGamble && index < 4; ++index)
        if (CheckCollisionPointRec(rv(mouse), tabBounds(index))) {
            if (!tabAvailable(shopView(), index)) return {};
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
    for (const auto &item : placements(shopView(), view_.shopCategory))
        if (item.page == view_.shopPage &&
            CheckCollisionPointRec(rv(mouse), item.bounds)) {
            view_.shopConfirm = item.offer->slot;
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
                           {panel.x + (index % 2) * 256 * scale,
                            panel.y + (index / 2) * 256 * scale,
                            tile.width * scale, tile.height * scale}, {0, 0}, 0, WHITE);
        }
    const auto &shop = shopView();
    for (int index = 0; !view_.shopGamble && index < 4; ++index) {
        auto tab = tabBounds(index);
        const bool available = tabAvailable(shopView(), index);
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
    auto items = placements(shopView(), view_.shopCategory);
    const ShopOfferView *hovered = nullptr;
    for (const auto &entry : items) {
        if (entry.page != view_.shopPage) continue;
        drawItemArt(entry.offer->artKey, entry.offer->definition, entry.bounds, WHITE);
        if (CheckCollisionPointRec(rv(mouse), entry.bounds)) {
            DrawRectangleLinesEx(entry.bounds, 1, gold);
            hovered = entry.offer;
        }
    }
    const std::string stashGold = std::to_string(shop.bankGold);
    painter_.label("STASH", int(panel.x + 20 * scale), int(panel.y + 362 * scale), 13, WHITE);
    painter_.label(stashGold, int(panel.x + 199 * scale) - painter_.measure(stashGold, 13),
                   int(panel.y + 362 * scale), 13, WHITE);
    const int buttonFrames[] = {2, 4, 6, 10};
    const bool repairAvailable = shop.repairAvailable;
    for (int index = 0; index < 4; ++index) {
        if (index == 2 && !repairAvailable) continue;
        auto sprite = assets_.vendorButtons.frame(0, buttonFrames[index]);
        if (!sprite) continue;
        const auto &texture = sprite->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                       shopButton(index), {0, 0}, 0,
                       index == 1 ? Color{125, 125, 125, 255} : WHITE);
        if (index == 2 && view_.shopRepair) DrawRectangleLinesEx(shopButton(index), 1, gold);
        if (index == 2 && CheckCollisionPointRec(rv(mouse), shopButton(index)))
            painter_.label("Repair", int(shopButton(index).x), int(shopButton(index).y - 20), 12, WHITE);
    }
    if (maximumPage(items) > (view_.shopCategory == 1 || view_.shopCategory == 2 ? 1 : 0)) {
        painter_.label("<", int(pageButton(false).x + 14), int(pageButton(false).y + 8), 15, gold);
        painter_.label(">", int(pageButton(true).x + 14), int(pageButton(true).y + 8), 15, gold);
        painter_.label(std::to_string(view_.shopPage + 1) + "/" +
                       std::to_string(maximumPage(items) + 1),
                       int(panel.x + 36 * scale), int(panel.y + 416 * scale), 11, gold);
    }
    if (hovered && !view_.shopConfirm && !view_.inventory.drag) {
        if (const auto *detail = npcClient_.inspectShopOffer(view_.dialogueObject, hovered->slot, view_.shopGamble))
            drawItemText(detail->tooltip, detail->quality, {mouse.x + 170, mouse.y}, detail->price, "Cost");
    }
    if (view_.shopConfirm) {
        auto popup = confirmBounds();
        if (auto sprite = assets_.vendorConfirm.frame(0, 0)) {
            const auto &texture = sprite->texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           popup, {0, 0}, 0, WHITE);
        }
        auto found = std::find_if(items.begin(), items.end(), [&](const Placement &entry) {
            return entry.offer->slot == *view_.shopConfirm;
        });
        if (found != items.end()) {
            const auto &name = found->offer->name;
            int fontSize = 16;
            while (fontSize > 1 && painter_.measure(name, fontSize) > popup.width - 44) --fontSize;
            painter_.inBox(name, {popup.x + 22, popup.y + 40, popup.width - 44, 32},
                           fontSize, itemColor(found->offer->quality));
            painter_.label(std::to_string(found->offer->price) + " GOLD?",
                           int(popup.x) + 24, int(popup.y) + 88, 14, parchment);
        }
        for (bool yes : {true, false}) {
            auto button = confirmButton(yes);
            DrawRectangleLinesEx(button, 1, gold);
            painter_.label(yes ? "YES" : "NO", int(button.x) + (yes ? 4 : 9),
                           int(button.y) + 8, 13, gold);
        }
    }
}
} // namespace d2x
