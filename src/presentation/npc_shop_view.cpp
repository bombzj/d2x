#include "scene_view.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
constexpr float scale = 1.25f;
Rectangle panelBounds() { return {16, 20, 400, 540}; }
Rectangle shopGrid() { return {16 + 14 * scale, 20 + 62 * scale,
                               10 * inventoryCellSize, 8 * inventoryCellSize}; }
Rectangle tabBounds(int index) { return {22 + index * 127.f, 35, 117, 40}; }
Rectangle backButton() { return {348, 519, 50, 39}; }
Rectangle pageButton(bool next) { return {next ? 293.f : 232.f, 519, 53, 35}; }
Rectangle confirmBounds() { return {89, 178, 265, 248}; }
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
        if (!item || (category == 0 ? item->family != ItemFamily::Armor :
                      category == 1 ? item->family != ItemFamily::Weapon :
                                      item->family != ItemFamily::Misc))
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
    if (!stock->empty()) {
        const auto *item = session_.inventory().catalog().find(stock->front().code);
        if (item) view_.shopCategory = item->family == ItemFamily::Armor ? 0 :
                                        item->family == ItemFamily::Weapon ? 1 : 2;
    }
    return true;
}
void SceneView::scrollNpcShop(int pages) {
    auto items = placements(session_, view_.dialogueObject, view_.shopCategory);
    view_.shopPage = std::clamp(view_.shopPage + pages, 0, maximumPage(items));
    view_.shopConfirm.reset();
}
std::optional<uint32_t> SceneView::clickNpcShop(Vec mouse) {
    if (view_.shopConfirm) {
        if (CheckCollisionPointRec(rv(mouse), confirmButton(true))) {
            auto slot = *view_.shopConfirm;
            view_.shopConfirm.reset();
            return slot;
        }
        if (CheckCollisionPointRec(rv(mouse), confirmButton(false)))
            view_.shopConfirm.reset();
        return {};
    }
    if (CheckCollisionPointRec(rv(mouse), backButton())) {
        view_.shopOpen = false;
        view_.npcMenu = true;
        view_.inventory.open = false;
        return {};
    }
    for (int index = 0; index < 3; ++index)
        if (CheckCollisionPointRec(rv(mouse), tabBounds(index))) {
            view_.shopCategory = index;
            view_.shopPage = 0;
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
    const char *labels[] = {"ARMOR", "WEAPONS", "MISC"};
    for (int index = 0; index < 3; ++index) {
        auto tab = tabBounds(index);
        if (auto sprite = assets_.vendorTabs.frame(0, view_.shopCategory == index ? index + 4 : index)) {
            const auto &texture = sprite->texture;
            DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                           tab, {0, 0}, 0, WHITE);
        }
        painter_.label(labels[index], int(tab.x) + 16, int(tab.y) + 10, 13, gold);
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
    if (hovered) {
        auto definition = session_.inventory().catalog().find(hovered->code);
        std::string label = definition ? definition->name : hovered->code;
        label += "  " + std::to_string(hovered->price) + " GOLD";
        painter_.label(label, 36, 460, 13, gold);
    }
    frame(pageButton(false)); frame(pageButton(true)); frame(backButton());
    painter_.label("<", 250, 526, 17, gold);
    painter_.label(">", 313, 526, 17, gold);
    painter_.label("X", 365, 526, 17, gold);
    painter_.label(std::to_string(view_.shopPage + 1) + " / " +
                   std::to_string(maximumPage(items) + 1), 174, 526, 13, gold);
    if (!view_.dialogueStatus.empty())
        painter_.label(view_.dialogueStatus, 28, 488, 12, gold);
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
        painter_.label("YES", int(confirmButton(true).x) + 3, int(confirmButton(true).y) + 8, 13, gold);
        painter_.label("NO", int(confirmButton(false).x) + 8, int(confirmButton(false).y) + 8, 13, gold);
    }
}
} // namespace d2x
