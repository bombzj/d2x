#include "presentation/scene_view.hpp"
#include "classic_panel.hpp"
#include "waypoint_panel.hpp"
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
    auto entries = travelEntries();
    const auto hit = waypointPanelHit(mouse, entries.size());
    if (hit.act) {
        if (mapView().waypointActs[size_t(*hit.act)]) view_.waypointAct = *hit.act;
        return {};
    }
    if (hit.close) {
        view_.travelMenu = false;
        view_.waypointSource = {};
        return {};
    }
    if (hit.row) {
        const auto &entry = entries[*hit.row];
        if (entry.destination)
            return entry.destination;
        notice(entry.status, true);
        return {};
    }
    return {};
}
WaypointPanelHit waypointPanelHit(Vec mouse, size_t rows) {
    const auto origin = panelOrigin();
    for (int act = 0; act < 5; ++act)
        if (CheckCollisionPointRec(rv(mouse), {origin.x + act * 61 * scaleX, origin.y, 61 * scaleX, 31 * scaleY}))
            return {act, {}, false};
    if (CheckCollisionPointRec(rv(mouse), waypointClose())) return {{}, {}, true};
    for (size_t row = 0; row < std::min(size_t(visibleWaypoints), rows); ++row)
        if (CheckCollisionPointRec(rv(mouse), waypointRow(int(row)))) return {{}, row, false};
    return {};
}

void SceneView::drawWaypointMenu(Vec mouse) const {
    const auto entries = travelEntries();
    drawWaypointPanel({assets_.waypointBorder, assets_.waypointPanel, assets_.waypointTabs,
        assets_.waypointIcons, assets_.questClose, assets_.waypointFonts, assets_.waypointTitle},
        entries, mapView().waypointActs, view_.waypointAct, int(mapView().region), mouse);
}
void drawWaypointPanel(const WaypointPanelArt &art, std::span<const TravelEntryView> entries,
    const std::array<bool, 5> &availableActs, int selectedAct, int currentLevel, Vec mouse) {
    drawClassicPanelFrame(art.border, false);
    auto origin = panelOrigin();
    for (int index = 0; index < 4; ++index)
        if (const auto *sprite = art.panel.frame(0, index))
            drawTile(art.panel, index,
                     {origin.x + (index % 2) * 256 * scaleX,
                      origin.y + (index / 2) * 256 * scaleY,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});
    int acts = std::min(5, art.tabs.count / 2);
    for (int act = 0; act < acts; ++act) {
        if (!availableActs[size_t(act)]) continue;
        if (const auto *sprite = art.tabs.frame(0, act * 2 + (act == selectedAct ? 0 : 1)))
            drawTile(art.tabs, act * 2 + (act == selectedAct ? 0 : 1),
                     {origin.x + act * 61 * scaleX, origin.y,
                      sprite->texture.width * scaleX, sprite->texture.height * scaleY});
    }

    const int textSize = int(std::round(16 * scaleY));
    const UiPainter white(art.fonts[0]);
    white.inBox(art.title,
                {origin.x, origin.y + 32 * scaleY, 320 * scaleX, 24 * scaleY}, textSize, WHITE);

    for (int row = 0; row < std::min(visibleWaypoints, int(entries.size())); ++row) {
        const auto &entry = entries[size_t(row)];
        auto bounds = waypointRow(row);
        const bool current = entry.level == currentLevel;
        const bool hovered = entry.destination && CheckCollisionPointRec(rv(mouse), bounds);
        // waygateicons: current location is the blue portal (0), other
        // activated locations are crossed markers (3). Locked sockets stay empty.
        if (entry.destination) {
            const int icon = current ? 0 : 3;
            if (const auto *sprite = art.icons.frame(0, icon))
                drawTile(art.icons, icon,
                         {bounds.x + 2 * scaleX, bounds.y,
                          sprite->texture.width * scaleX, sprite->texture.height * scaleY});
        }
        const UiPainter text(art.fonts[!entry.destination ? 2 : current || hovered ? 1 : 0]);
        text.inBox(entry.name,
                   {origin.x + 80 * scaleX, bounds.y,
                    float(text.measure(entry.name, textSize)), bounds.height}, textSize, WHITE);
    }
    drawTile(art.close, 10, waypointClose());
}
void loadWaypointFonts(Graphics &graphics, Archives &archives, const ClassicFont &base,
                       std::array<ClassicFont, 3> &fonts) {
    // OpenDiablo2 PL2.TextColorShifts: thirteen RGB triples follow the blend
    // transforms at 0x6B600, then thirteen 256-entry font index transforms.
    constexpr size_t shifts = 0x6B600 + 13 * 3;
    const auto palette = archives.read("data/global/palette/sky/pal.pl2");
    const auto *glyphs = graphics.animation("data/local/font/latin/font16.dc6");
    if (palette.size() < shifts + 13 * 256 || !glyphs)
        throw std::runtime_error("Original waypoint font transforms are missing");
    constexpr int colors[]{0, 3, 5};
    for (size_t index = 0; index < fonts.size(); ++index) {
        auto &font = fonts[index];
        font = base;
        // White uses the original glyph indices. The sky PL2 white table is
        // all zeroes, so applying it would make every glyph transparent.
        if (colors[index] == 0) continue;
        font.glyphs.frames.clear();
        for (auto glyph : glyphs->frames) {
            for (auto &pixel : glyph.pixels)
                if (pixel) pixel = palette[shifts + colors[index] * 256 + pixel];
            font.glyphs.frames.push_back(graphics.upload(glyph));
        }
    }
}
} // namespace d2x
