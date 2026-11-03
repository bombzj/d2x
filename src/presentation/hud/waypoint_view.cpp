#include "presentation/scene_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include "classic_panel.hpp"
#include <algorithm>
#include <array>

namespace d2x {
namespace {
// Retail 1.13c D2Client row records (RVA DE6D8). The icon bottoms and
// hit rectangles use a different pitch from the text baselines.
constexpr std::array rowHitTop{60, 96, 132, 168, 205, 241, 277, 313, 349};

Vec panelPoint(int x, int y) {
    const auto panel = classicPanelBounds(false);
    return {panel.x + x * classicPanelScale(), panel.y + y * classicPanelScale()};
}
Rectangle panelRect(int x, int y, int width, int height) {
    const auto at = panelPoint(x, y);
    return {at.x, at.y, width * classicPanelScale(), height * classicPanelScale()};
}
Rectangle closeHit() { return panelRect(273, 387, 36, 34); }

// DC6 controls take their drawing origin at the bottom of the cell.
void drawCell(const GpuAnimation &art, int frame, int x, int bottom) {
    const auto *cell = art.frame(0, frame);
    if (!cell) return;
    const auto &t = cell->texture;
    DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
                   panelRect(x + cell->x, bottom + cell->y - t.height, t.width, t.height), {}, 0, WHITE);
}
} // namespace

std::optional<RegionId> SceneView::clickWaypointMenu(Vec mouse) {
    for (int act = 0; act < 5; ++act) {
        if (CheckCollisionPointRec(rv(mouse), panelRect(act * 64, 0, 64, 31))) {
            if (mapView().waypointActs[size_t(act)]) view_.waypointAct = act;
            return {};
        }
    }
    if (CheckCollisionPointRec(rv(mouse), closeHit())) {
        view_.travelMenu = false;
        view_.waypointSource = {};
        return {};
    }
    const auto entries = travelEntries();
    for (size_t row = 0; row < std::min(rowHitTop.size(), entries.size()); ++row) {
        // The native comparisons exclude both edges of the 280 x 30 area.
        if (CheckCollisionPointRec(rv(mouse), panelRect(18, rowHitTop[row] + 1, 279, 29))) {
            if (entries[row].level != int(mapView().region)) return entries[row].destination;
            return {};
        }
    }
    return {};
}

void SceneView::drawWaypointMenu(Vec mouse) const {
    const auto entries = travelEntries();
    const auto &map = mapView();
    const int palette = std::clamp(map.palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    drawPanelFrame(false);
    for (int i = 0; i < 4; ++i)
        drawCell(assets_.waypointPanel, i, (i % 2) * 256, i < 2 ? 256 : 432);
    for (int act = 0; act < 5; ++act) {
        if (act == view_.waypointAct || map.waypointActs[size_t(act)])
            drawCell(assets_.waypointTabs, act * 2 + int(act != view_.waypointAct), 5 + act * 62, 34);
    }
    drawCell(assets_.questClose, 10, 273, 417);

    const float scale = classicPanelScale();
    const auto &white = assets_.itemTextFonts[0];
    const auto &currentFont = assets_.itemTextFonts[3];
    const auto &unavailableFont = assets_.itemTextFonts[5];
    bool hasDestination = false;
    for (size_t row = 0; row < std::min(rowHitTop.size(), entries.size()); ++row) {
        const auto &entry = entries[row];
        const bool current = entry.level == int(map.region);
        if (current || entry.destination)
            drawCell(assets_.waypointIcons, current ? 0 : 3, 17, rowHitTop[row] + 29);
        if (!current && entry.destination) hasDestination = true;
        const auto &font = current ? currentFont : entry.destination ? white : unavailableFont;
        font.drawBaseline(entry.name, panelPoint(80, 84 + 35 * int(row)), scale);
    }
    const auto &title = hasDestination ? assets_.waypointTitle : assets_.waypointNoDestinationsTitle;
    white.drawBaseline(title, panelPoint(160 - white.textWidth(title) / 2, 48), scale);

    if (CheckCollisionPointRec(rv(mouse), closeHit())) {
        const auto &hint = assets_.waypointCloseHint;
        const int halfWidth = white.textWidth(hint) / 2;
        if (blend)
            blend->drawRectangle(panelRect(287 - halfWidth, 370, 7 + 2 * halfWidth, 17), BLACK);
        white.drawBaseline(hint, panelPoint(292 - halfWidth, 385), scale);
    }
}
} // namespace d2x
