#include "presentation/scene_view.hpp"
#include "classic_panel.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
constexpr float scaleX = classicPanelScale, scaleY = classicPanelScale;
constexpr int visibleWaypoints = 9;
Vec panelOrigin() { const auto panel = classicPanelBounds(false); return {panel.x, panel.y}; }
Rectangle waypointRow(int row) {
    auto origin = panelOrigin();
    return {origin.x + 14 * scaleX, origin.y + (58 + row * 36) * scaleY,
            294 * scaleX, 35 * scaleY};
}
Rectangle waypointClose() {
    auto origin = panelOrigin();
    return {origin.x + 272 * scaleX, origin.y + 385 * scaleY, 32 * scaleX, 32 * scaleY};
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
    const auto origin = panelOrigin();
    for (int act = 0; act < int(mapView().waypointActs.size()); ++act)
        if (CheckCollisionPointRec(rv(mouse), {origin.x + act * 61 * scaleX, origin.y, 61 * scaleX, 31 * scaleY})) {
            if (mapView().waypointActs[size_t(act)]) view_.waypointAct = act;
            return {};
        }
    if (CheckCollisionPointRec(rv(mouse), waypointClose())) {
        view_.travelMenu = false;
        view_.waypointSource = {};
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
    const auto &scene = mapView();
    for (int act = 0; act < acts; ++act) {
        if (!scene.waypointActs[size_t(act)]) continue;
        if (const auto *sprite = assets_.waypointTabs.frame(0, act * 2 + (act == view_.waypointAct ? 0 : 1)))
            drawTile(assets_.waypointTabs, act * 2 + (act == view_.waypointAct ? 0 : 1),
                     {origin.x + act * 61 * scaleX, origin.y,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});
    }

    const int textSize = int(std::round(16 * scaleY));
    const UiPainter white(assets_.waypointFonts[0]);
    white.inBox(assets_.waypointTitle,
                {origin.x, origin.y + 32 * scaleY, 320 * scaleX, 24 * scaleY}, textSize, WHITE);

    auto entries = travelEntries();
    for (int row = 0; row < std::min(visibleWaypoints, int(entries.size())); ++row) {
        const auto &entry = entries[size_t(row)];
        auto bounds = waypointRow(row);
        const bool current = entry.level == int(scene.region);
        const bool hovered = entry.destination && CheckCollisionPointRec(rv(mouse), bounds);
        // waygateicons: current location is the blue portal (0), other
        // activated locations are crossed markers (3). Locked sockets stay empty.
        if (entry.destination) {
            const int icon = current ? 0 : 3;
            if (const auto *sprite = assets_.waypointIcons.frame(0, icon))
                drawTile(assets_.waypointIcons, icon,
                         {bounds.x + 2 * scaleX, bounds.y,
                          sprite->texture.width * scaleX, sprite->texture.height * scaleY});
        }
        const UiPainter text(assets_.waypointFonts[!entry.destination ? 2 : current || hovered ? 1 : 0]);
        text.inBox(entry.name,
                   {origin.x + 80 * scaleX, bounds.y,
                    float(text.measure(entry.name, textSize)), bounds.height}, textSize, WHITE);
    }
    drawTile(assets_.questClose, 10, waypointClose());
}
} // namespace d2x
