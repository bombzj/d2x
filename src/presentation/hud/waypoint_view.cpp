#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
namespace {
constexpr float scaleX = classicPanelScale, scaleY = classicPanelScale;
constexpr int visibleWaypoints = 9;
Vec panelOrigin() { return {80 * scaleX, 56 * scaleY}; }
Rectangle waypointRow(int row) {
    auto origin = panelOrigin();
    return {origin.x + 12 * scaleX, origin.y + (56 + row * 37) * scaleY,
            302 * scaleX, 35 * scaleY};
}
Rectangle waypointClose() {
    auto origin = panelOrigin();
    return {origin.x + 270 * scaleX, origin.y + 383 * scaleY, 42 * scaleX, 40 * scaleY};
}
void drawTile(const GpuAnimation &art, int frame, Rectangle bounds) {
    auto sprite = art.frame(0, frame);
    if (!sprite) return;
    const auto &texture = sprite->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)},
                   bounds, {0, 0}, 0, WHITE);
}
} // namespace

std::optional<RegionId> SceneView::clickWaypointMenu(Vec mouse) {
    if (CheckCollisionPointRec(rv(mouse), waypointClose())) {
        view_.travelMenu = false;
        return {};
    }
    auto entries = travelEntries();
    for (int row = 0; row < std::min(visibleWaypoints, int(entries.size())); ++row) {
        if (!CheckCollisionPointRec(rv(mouse), waypointRow(row)))
            continue;
        const auto &entry = entries[size_t(row)];
        if (entry.destination)
            return entry.destination;
        notice(entry.status, true);
        return {};
    }
    return {};
}

void SceneView::drawWaypointMenu(Vec mouse) const {
    drawPanelFrame(false);
    auto origin = panelOrigin();
    for (int index = 0; index < 4; ++index)
        if (const auto *sprite = assets_.waypointPanel.frame(0, index))
            drawTile(assets_.waypointPanel, index,
                     {origin.x + (index % 2) * 256 * scaleX,
                      origin.y + (index / 2) * 256 * scaleY,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});
    int acts = std::min(5, assets_.waypointTabs.count / 2);
    for (int act = 0; act < acts; ++act)
        if (const auto *sprite = assets_.waypointTabs.frame(0, act * 2 + (act == 0 ? 0 : 1)))
            drawTile(assets_.waypointTabs, act * 2 + (act == 0 ? 0 : 1),
                     {origin.x + act * 63 * scaleX, origin.y,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});

    auto entries = travelEntries();
    for (int row = 0; row < std::min(visibleWaypoints, int(entries.size())); ++row) {
        const auto &entry = entries[size_t(row)];
        auto bounds = waypointRow(row);
        bool hovered = CheckCollisionPointRec(rv(mouse), bounds);
        int icon = entry.destination ? (hovered ? 1 : 0) : 3;
        if (const auto *sprite = assets_.waypointIcons.frame(0, icon))
            drawTile(assets_.waypointIcons, icon,
                     {bounds.x + 2 * scaleX, bounds.y + 2 * scaleY,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});
        painter_.label(entry.name, int(bounds.x + 43 * scaleX), int(bounds.y + 5 * scaleY), 16,
                       entry.destination ? (hovered ? gold : parchment) : Color{125, 119, 106, 255});
    }
}
} // namespace d2x
