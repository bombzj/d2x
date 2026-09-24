#include "scene_view.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
constexpr float scale = 1.25f;
Rectangle panelBounds() {
    auto inventory = inventoryBounds();
    return {inventory.x - inventory.width, inventory.y, inventory.width, inventory.height};
}
Rectangle shopGrid() {
    auto panel = panelBounds();
    return {panel.x + 14 * scale, panel.y + 62 * scale,
            10 * inventoryCellSize, 8 * inventoryCellSize};
}
Rectangle tabBounds(int index) {
    auto panel = panelBounds();
    return {panel.x + index * 80 * scale, panel.y, 80 * scale, 28 * scale};
}
Rectangle shopButton(int index) {
    auto panel = panelBounds();
    return {panel.x + (112 + index * 52) * scale, panel.y + 400 * scale,
            32 * scale, 32 * scale};
}
Rectangle backButton() { return shopButton(3); }
Rectangle pageButton(bool next) {
    auto panel = panelBounds();
    return {panel.x + (next ? 76.f : 18.f), panel.y + 500, 48, 34};
}
Rectangle confirmBounds() {
    auto panel = panelBounds();
    return {panel.x + 73, panel.y + 158, 265, 248};
}
Rectangle confirmButton(bool yes) {
    auto bounds = confirmBounds();
    return {bounds.x + (yes ? 43.f : 174.f), bounds.y + 184, 45, 46};
}
struct Placement { const VendorOffer *offer; int page; Rectangle bounds; };
std::vector<Placement> placements(const GameSession &session, EntityId npc, int category) {
    std::vector<Placement> result;
    const auto *stock = session.vendorStock(npc);
    if (!stock) return result;
    std::array<bool, 80> occupied{};
    int page = 0;
    auto grid = shopGrid();
    for (const auto &offer : *stock) {
        if (session.vendorOfferSold(npc, offer.slot)) continue;
        const auto *item = session.inventory().catalog().find(offer.code);
        if (!item || offer.storePage != (category == 2 ? 1 : category))
            continue;
        if (item->width < 1 || item->height < 1 || item->width > 10 || item->height > 8)
            continue;
        auto locate = [&]() -> std::optional<Cell> {
            for (int y = 0; y <= 8 - item->height; ++y)
                for (int x = 0; x <= 10 - item->width; ++x) {
                    bool free = true;
                    for (int yy = y; yy < y + item->height; ++yy)
                        for (int xx = x; xx < x + item->width; ++xx)
                            free &= !occupied[size_t(yy * 10 + xx)];
                    if (free) return Cell{x, y};
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
        for (int y = cell->y; y < cell->y + item->height; ++y)
            for (int x = cell->x; x < cell->x + item->width; ++x)
                occupied[size_t(y * 10 + x)] = true;
        result.push_back({&offer, page,
                          {grid.x + cell->x * inventoryCellSize,
                           grid.y + cell->y * inventoryCellSize,
                           item->width * inventoryCellSize,
                           item->height * inventoryCellSize}});
    }
    return result;
}
int maximumPage(const std::vector<Placement> &items) {
    return items.empty() ? 0 : items.back().page;
}
bool tabAvailable(const GameSession &session, EntityId npc, int tab) {
    auto items = placements(session, npc, tab);
    return tab == 2 ? maximumPage(items) > 0 : !items.empty();
}
} // namespace
bool SceneView::openNpcShop() {
    const auto *stock = session_.vendorStock(view_.dialogueObject);
    if (!stock || !view_.npcMenu) return false;
    view_.npcMenu = false;
    view_.shopOpen = true;
    view_.shopPage = 0;
    view_.shopConfirm.reset();
    view_.inventory.open = true;
    view_.shopCategory = 0;
    for (int tab = 0; tab < 4; ++tab)
        if (tabAvailable(session_, view_.dialogueObject, tab)) {
            view_.shopCategory = tab;
            view_.shopPage = tab == 2 ? 1 : 0;
            break;
        }
    return true;
}
void SceneView::scrollNpcShop(int pages) {
    auto items = placements(session_, view_.dialogueObject, view_.shopCategory);
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
        for (const auto &item : placements(session_, view_.dialogueObject, view_.shopCategory))
            if (item.page == view_.shopPage && CheckCollisionPointRec(rv(mouse), item.bounds))
                return item.offer->slot;
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), backButton())) {
        view_.shopOpen = false;
        view_.npcMenu = true;
        view_.inventory.open = false;
        return {};
    }
    for (int index = 0; index < 4; ++index)
        if (CheckCollisionPointRec(rv(mouse), tabBounds(index))) {
            if (!tabAvailable(session_, view_.dialogueObject, index)) return {};
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
    for (const auto &item : placements(session_, view_.dialogueObject, view_.shopCategory))
        if (item.page == view_.shopPage &&
            CheckCollisionPointRec(rv(mouse), item.bounds)) {
            view_.shopConfirm = item.offer->slot;
            return {};
        }
    return {};
}
void SceneView::drawNpcShop(Vec mouse) const {
    auto panel = panelBounds();
    DrawRectangle(int(panel.x) - 6, 0, int(panel.width) + 12, H - HUD, {0, 0, 0, 150});
    if (assets_.vendorPanel.frames.size() >= 4)
        for (int index = 0; index < 4; ++index) {
            const auto &tile = assets_.vendorPanel.frames[size_t(index)].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                           {panel.x + (index % 2) * 256 * scale,
                            panel.y + (index / 2) * 256 * scale,
                            tile.width * scale, tile.height * scale}, {0, 0}, 0, WHITE);
        }
    const auto &pages = session_.content().tables.at("storepage");
    for (int index = 0; index < 4; ++index) {
        auto tab = tabBounds(index);
        const bool available = tabAvailable(session_, view_.dialogueObject, index);
        bool selected = index == view_.shopCategory;
        if (auto sprite = assets_.vendorTabs.frame(0, selected ? index + 4 : index)) {
            const auto &texture = sprite->texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           tab, {0, 0}, 0, WHITE);
        }
        std::string label(pages.value(size_t(index == 2 ? 1 : index), "Store Page"));
        if (label.ends_with(" Page")) label.resize(label.size() - 5);
        if (available)
            painter_.label(label, int(tab.x + (tab.width - painter_.measure(label, 13)) / 2),
                           int(tab.y) + 9, 13, gold);
    }
    auto items = placements(session_, view_.dialogueObject, view_.shopCategory);
    const VendorOffer *hovered = nullptr;
    for (const auto &entry : items) {
        if (entry.page != view_.shopPage) continue;
        ItemInstance visual;
        visual.definition = entry.offer->code;
        visual.quantity = entry.offer->quantity;
        visual.defense = entry.offer->defense;
        drawItemIcon(visual, entry.bounds);
        if (CheckCollisionPointRec(rv(mouse), entry.bounds)) {
            DrawRectangleLinesEx(entry.bounds, 1, gold);
            hovered = entry.offer;
        }
    }
    if (hovered && !view_.shopConfirm) {
        auto definition = session_.inventory().catalog().find(hovered->code);
        const std::string name = definition ? definition->name : hovered->code;
        const std::string cost = "Cost: " + std::to_string(hovered->price);
        const std::string quantity = "Quantity: " + std::to_string(hovered->quantity);
        const int width = std::min(int(panel.width) - 16,
            std::max({painter_.measure(name, 13), painter_.measure(cost, 13),
                      hovered->quantity > 1 ? painter_.measure(quantity, 13) : 0}) + 20);
        const int height = hovered->quantity > 1 ? 65 : 47;
        const float x = std::clamp(mouse.x + 14.f, panel.x + 8.f,
                                   panel.x + panel.width - width - 8.f);
        const float y = std::clamp(mouse.y - float(height) - 8.f, panel.y + 45.f,
                                   panel.y + panel.height - height - 12.f);
        DrawRectangleRec({x, y, float(width), float(height)}, {0, 0, 0, 230});
        painter_.label(name, int(x) + 10, int(y) + 7, 13, gold);
        painter_.label(cost, int(x) + 10, int(y) + 24, 13, parchment);
        if (hovered->quantity > 1)
            painter_.label(quantity, int(x) + 10, int(y) + 41, 13, parchment);
    }
    painter_.label("GOLD: " + std::to_string(session_.state().player.gold),
                   int(panel.x + 27), int(panel.y + 453), 13, parchment);
    const int buttonFrames[] = {2, 4, 6, 10};
    for (int index = 0; index < 4; ++index) {
        auto sprite = assets_.vendorButtons.frame(0, buttonFrames[index]);
        if (!sprite) continue;
        const auto &texture = sprite->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                       shopButton(index), {0, 0}, 0,
                       index == 1 || index == 2 ? Color{125, 125, 125, 255} : WHITE);
    }
    if (maximumPage(items) > (view_.shopCategory == 1 || view_.shopCategory == 2 ? 1 : 0)) {
        painter_.label("<", int(pageButton(false).x + 14), int(pageButton(false).y + 8), 15, gold);
        painter_.label(">", int(pageButton(true).x + 14), int(pageButton(true).y + 8), 15, gold);
        painter_.label(std::to_string(view_.shopPage + 1) + "/" +
                       std::to_string(maximumPage(items) + 1),
                       int(panel.x + 38), int(panel.y + 530), 11, gold);
    }
    if (!view_.dialogueStatus.empty())
        painter_.label(view_.dialogueStatus, int(panel.x + 12), int(panel.y + 468), 12, gold);
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
            auto definition = session_.inventory().catalog().find(found->offer->code);
            painter_.label(definition ? definition->name : found->offer->code,
                           int(popup.x) + 22, int(popup.y) + 47, 14, gold);
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
