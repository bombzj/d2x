#include "presentation/scene_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include "presentation/world/automap_projection.hpp"
#include "presentation/world/palette_blend_view.hpp"

namespace d2x {
void SceneView::drawBhAutomapLines(const AutomapDrawView &map, const AutomapProjection &projection,
        Rectangle area, const PaletteBlendView &blend) const {
    if (!view_.bhEnabled) return;
    if (view_.bhShowPathLine && !map.pathLines.empty()) {
        // Existing D2X guidance uses the BH point projection. Its color and
        // endpoint styling are not yet certified against a BH version.
        const auto from = projection.bhPoint(map.observer);
        uiScissor(area.x, area.y, area.width, area.height);
        for (const auto &line : map.pathLines) {
            const auto to = projection.bhPoint(line.position);
            const Color color = line.kind == 0 ? Color{80, 200, 255, 220}   // waypoint: cyan
                              : line.kind == 1 ? Color{255, 220, 80, 220}   // exit: gold
                                               : Color{120, 255, 120, 220}; // quest: green
            DrawLineV(rv(from), rv(to), color);
            DrawCircleV(rv(to), 3.f, color);
        }
        uiScissor(0, 0, float(W), float(H));
    }
    if (view_.bhShowLkChestLines && !map.lkChestLines.empty()) {
        // CN BH "LK Chest Lines": player dot to each Lower Kurast superchest
        // preset, palette color 105 (CN default), same clip as the path lines.
        uiScissor(area.x, area.y, area.width, area.height);
        const auto from = projection.bhPoint(map.observer);
        const Color color = blend.paletteByte(105);
        for (const auto &target : map.lkChestLines)
            DrawLineV(rv(from), rv(projection.bhPoint(target)), color);
        uiScissor(0, 0, float(W), float(H));
    }
}
void SceneView::drawBhAutomapMarker(const AutomapDrawView::Marker &marker, const AutomapProjection &projection,
        Rectangle area, Vec miniOrigin, const PaletteBlendView &blend) const {
    if (!view_.bhEnabled) return;
    const bool large = view_.automapLarge;
    const auto at = projection.bhPoint(marker.position);
    const auto unitVisible = [&](Vec point) {
        const Vec begin = large ? Vec{-16, -16} : miniOrigin + Vec{-8, 0};
        const Vec end = large ? Vec{float(W), float(H)} : miniOrigin + Vec{float(W / 3), float(H / 3)};
        return point.x >= begin.x && point.y >= begin.y && point.x <= end.x && point.y <= end.y;
    };
    const auto cross = [&](Color color) {
        if (!unitVisible(at)) return;
        // Current CN screenshot supplied by the user: one-pixel axial '+'.
        // Public BH Crosshook uses a twelve-segment outline instead; retain
        // NPCs and other native unit marks always retain the original path.
        DrawLineV(rv(at + Vec{-4,0}), rv(at + Vec{5,0}), color);
        DrawLineV(rv(at + Vec{0,-4}), rv(at + Vec{0,5}), color);
    };
    using Mark = AutomapDrawView::UnitMark;
    switch (marker.unitMark) {
        // CN '+' with BH's palette-byte rank color.
        case Mark::Monster: {
            cross(blend.paletteByte(marker.markColor));
            // Retain the existing text rows; the BH/CN font baseline and
            // per-version text offsets still require a separate comparison.
            auto markText = [&](const std::string &text, float lift) {
                if (text.empty()) return;
                if (!unitVisible(at)) return;
                int width = 0;
                for (size_t i = 1; i < text.size(); i += 2)
                    width += assets_.characterLabelFont.widthOf(uint8_t(text[i]));
                float x = at.x - width / 2.f;
                for (size_t i = 1; i < text.size(); i += 2) {
                    const size_t color = size_t(text[i - 1] - '0');
                    const auto &font = color < assets_.automapTextFonts.size() &&
                            assets_.automapTextFonts[color].ready
                        ? assets_.automapTextFonts[color]
                        : assets_.characterLabelFont;
                    if (const auto *glyph = font.glyphOf(uint8_t(text[i])); glyph)
                        DrawTexture(glyph->texture, int(x + glyph->x),
                            int(at.y - lift + glyph->y - glyph->texture.height), WHITE);
                    x += font.widthOf(uint8_t(text[i]));
                }
            };
            markText(marker.markEnchant, 22);
            markText(marker.markResist, 10);
            break;
        }
        case Mark::Quest:
            if (unitVisible(at))
                for (size_t i = 1; i < automapUnitOutline.size(); ++i)
                    DrawLineV(rv(at + automapUnitOutline[i - 1] * 2),
                        rv(at + automapUnitOutline[i] * 2), {120,255,120,255});
            break;
        // Retain the existing 3x3 box until native endpoint comparison.
        case Mark::Chest: {
            if (at.x >= area.x && at.y >= area.y && at.x < area.x + area.width &&
                at.y < area.y + area.height)
                DrawRectangle(int(at.x) - 1, int(at.y) - 1, 3, 3, blend.paletteByte(255));
            break;
        }
    default: break;
    }
}
} // namespace d2x
