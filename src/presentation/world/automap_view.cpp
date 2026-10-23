#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SceneView::drawAutomap(const AutomapDrawView &map) {
    if (!view_.automap) return;
    const bool large = view_.automapLarge;
    const auto viewport = worldViewport();
    const bool leftPanel = viewport.x > 0, rightPanel = viewport.x + viewport.width < W;
    const bool right = leftPanel != rightPanel ? leftPanel : view_.minimapRight;
    const auto layout = std::tuple{W, H, large, !large && right};
    // RVA 5F9F0 forces a recenter when a side panel changes the effective
    // mini side. Resolution/mode changes also force it, even with Center off.
    if (automapLayout_ && *automapLayout_ != layout) view_.automapOffset = {};
    automapLayout_ = layout;
    // D2Client 1.13c RVA 5F9F0 / 60250: mini origin/culling uses thirds,
    // but its actual CelDraw crop is a fixed 280 by 226 inclusive rectangle.
    const Vec miniOrigin{right ? float(W * 2 / 3) : 0.f,
        right ? 78.f : W >= 800 && H >= 600 ? 0.f : 96.f};
    const Rectangle area = large ? Rectangle{0, 0, float(W), float(H)}
        : Rectangle{right ? float(W - 281) : 0.f, right ? 57.f : miniOrigin.y - 21, 280, 226};
    const float sideShift = large && leftPanel != rightPanel ? (leftPanel ? W / 4 : -W / 4) : 0;
    const int divisor = large ? 10 : 20;
    // The native camera is a projected top-left, with a 16-pixel vertical
    // bias (RVA 3F860). Divide camera and cell separately, truncating to zero.
    const auto observer = project(map.observer);
    const Vec camera{float(int(observer.x) - W / 2), float(int(observer.y) - H / 2 + 16)};
    const Vec cleared = large ? Vec{} : Vec{float(W / 3) - miniOrigin.x - 16,
        float(H / 3) - miniOrigin.y - 16};
    const Vec origin{float(int(camera.x) / divisor - W / 2 + 40) - sideShift + cleared.x,
        float(int(camera.y) / divisor - (H - 40) / 2 + 15) + cleared.y};
    auto onMap = [&](Vec world) {
        const auto pixel = project(world);
        return Vec{float(int(pixel.x) / divisor), float(int(pixel.y) / divisor)} - origin + view_.automapOffset;
    };
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    if (!blend) return;
    // RVA 60346: Auto reads Unit.nAct, not its animation mode.
    const bool autoFade = mapView().act == 0 || mapView().act == 2;
    auto fadeMode = [&](Vec position, bool town) {
        if (view_.automapFade == AutomapFade::Everything ||
            (view_.automapFade == AutomapFade::Auto && autoFade)) return large ? 2 : 1;
        if (view_.automapFade != AutomapFade::Center || !large || town) return 5;
        // RVA 60391..6045B: one mode for the entire cell, selected from its
        // anchor plus (8,16); the native center is fixed while the map pans.
        const auto delta = position + Vec{8, 16} - Vec{float(W / 2) + sideShift, float((H - 40) / 2 - 10)};
        const int x = std::abs(int(delta.x)), y = std::abs(int(delta.y));
        if (x > 140 || y > 140) return 5;
        const int distance = (2 * std::max(x, y) + std::min(x, y)) / 2;
        return distance < 50 ? 0 : distance < 100 ? 1 : distance < 150 ? 2 : 5;
    };
    auto mapSprite = [&](const Sprite &image, Vec position, bool town = false) {
        const float left = position.x + image.x, top = position.y + image.y;
        if (left >= area.x + area.width || top >= area.y + area.height ||
            left + image.texture.width <= area.x || top + image.texture.height <= area.y) return;
        blend->drawAutomap(&image, position, fadeMode(position, town));
    };
    auto unitVisible = [&](Vec at) {
        // Native units are accepted by their anchor before the mini glyph
        // adjustment, independently of the DC6 crop rectangle.
        const Vec begin = large ? Vec{-16, -16} : miniOrigin + Vec{-8, 0};
        const Vec end = large ? Vec{float(W), float(H)} : miniOrigin + Vec{float(W / 3), float(H / 3)};
        return at.x >= begin.x && at.y >= begin.y && at.x <= end.x && at.y <= end.y;
    };
    // D2Client 1.13c 0x5F1C0 / 0xD2DE8: twelve connected original
    // automap line segments. These unit marks are not DC6 cells 221/317.
    auto unitMarkAt = [&](Vec world, Color color) {
        auto at = onMap(world) + Vec{8, -8};
        if (!unitVisible(at)) return;
        if (!large) at = at + Vec{-1, 5};
        constexpr std::array<Vec, 13> points{{{0,-1}, {2,-2}, {4,-1}, {2,0}, {4,1}, {2,2},
            {0,1}, {-2,2}, {-4,1}, {-2,0}, {-4,-1}, {-2,-2}, {0,-1}}};
        for (size_t i = 1; i < points.size(); ++i)
            DrawLineV(rv(at + points[i - 1] * 2), rv(at + points[i] * 2), color);
    };
    auto unitMark = [&](Vec world, Color rgb) { unitMarkAt(world, blend->paletteColor(rgb)); };
    uiScissor(area.x, area.y, area.width, area.height);
    BeginBlendMode(BLEND_ALPHA);
    for (const auto &stamp : map.stamps)
        if (const auto *image = assets_.automapSprite(stamp.cel, large))
            mapSprite(*image, onMap(stamp.position));
    for (const auto &town : map.towns)
        for (const auto &cell : assets_.townAutomapSprites(town.level, town.variant, large))
            mapSprite(cell.image, onMap(unproject(project(town.center) + cell.offset * 10)), true);
    for (const auto &marker : map.markers) {
        using Mark = AutomapDrawView::UnitMark;
        if ((marker.unitMark == Mark::Party || marker.unitMark == Mark::PartyPet) && !view_.automapParty) continue;
        if (const auto *image = assets_.automapSprite(marker.cel, large))
            mapSprite(*image, onMap(marker.position));
    }
    EndBlendMode();
    EndScissorMode();
    uiScissor(0, 0, float(W), float(H));
    if (view_.bhShowPathLine && !map.pathLines.empty()) {
        // BH path lines run from the player's own automap dot to the area
        // targets, clipped to the active automap rectangle (mini or full).
        const auto from = onMap(map.observer);
        uiScissor(area.x, area.y, area.width, area.height);
        for (const auto &line : map.pathLines) {
            const auto to = onMap(line.position);
            const Color color = line.kind == 0 ? Color{80, 200, 255, 220}   // waypoint: cyan
                              : line.kind == 1 ? Color{255, 220, 80, 220}   // exit: gold
                                               : Color{120, 255, 120, 220}; // quest: green
            DrawLineV(rv(from), rv(to), color);
            DrawCircleV(rv(to), 3.f, color);
        }
        uiScissor(0, 0, float(W), float(H));
    }
    for (const auto &marker : map.markers) {
        using Mark = AutomapDrawView::UnitMark;
        if ((marker.unitMark == Mark::Party || marker.unitMark == Mark::PartyPet) && !view_.automapParty) continue;
        switch (marker.unitMark) {
        case Mark::Npc: unitMark(marker.position, {244,244,244,255}); break;
        case Mark::Party: unitMark(marker.position, {0,255,0,255}); break;
        case Mark::OtherPlayer: unitMark(marker.position, {255,0,0,255}); break;
        case Mark::Corpse: unitMark(marker.position, {255,0,255,255}); break;
        case Mark::OwnPet: unitMark(marker.position, {68,112,116,255}); break;
        case Mark::PartyPet: unitMark(marker.position, {72,160,52,255}); break;
        case Mark::Stash: if(!view_.automapNames) unitMark(marker.position,{0,0,0,255}); break;
        case Mark::BluePortal:
        case Mark::RedPortal: unitMark(marker.position, {244,244,0,255}); break;
        // BH Crosshook: the same twelve segments, colored by the BH palette byte.
        case Mark::Monster: {
            unitMarkAt(marker.position, blend->paletteByte(marker.markColor));
            // BH draws enchantment letters at y-14 and resistance letters at
            // y-8, centered on the cross, each in its PL2 text color.
            auto markText = [&](const std::string &text, float lift) {
                if (text.empty()) return;
                auto at = onMap(marker.position) + Vec{8, -8};
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
            markText(marker.markEnchant, 16);
            markText(marker.markResist, 10);
            break;
        }
        case Mark::Quest: unitMarkAt(marker.position, {120,255,120,255}); break;
        // BH Boxhook 3x3 in palette byte 255 over unopened containers.
        case Mark::Chest: {
            const auto at = onMap(marker.position);
            if (at.x >= area.x && at.y >= area.y && at.x < area.x + area.width &&
                at.y < area.y + area.height)
                DrawRectangle(int(at.x) - 1, int(at.y) - 1, 3, 3, blend->paletteByte(255));
            break;
        }
        default: break;
        }
    }
    if (view_.automapNames) for (const auto &marker : map.markers) {
        if (!marker.showName) continue;
        using Mark=AutomapDrawView::UnitMark;
        if ((marker.unitMark == Mark::Party || marker.unitMark == Mark::OtherPlayer || marker.unitMark==Mark::Corpse) && !view_.automapParty) continue;
        auto position = onMap(marker.position) + Vec{8,-8};
        if (!unitVisible(position)) continue;
        const auto &font = marker.unitMark == AutomapDrawView::UnitMark::Party ? assets_.automapPartyFont :
            marker.unitMark==Mark::OtherPlayer || marker.unitMark==Mark::Corpse ? assets_.automapOtherFont :
            marker.npc ? assets_.automapNpcFont : assets_.characterLabelFont;
        int width = 0;
        for (uint32_t c : utf8Decode(marker.name)) width += font.widthOf(c);
        float x = position.x - width / 2;
        for (uint32_t c : utf8Decode(marker.name)) {
            if (const auto *glyph = font.glyphOf(c); glyph && c != ' ')
                DrawTexture(glyph->texture, int(x + glyph->x), int(position.y - 10 + glyph->y - glyph->texture.height), WHITE);
            x += font.widthOf(c);
        }
    }
    if(map.observerVisible) unitMark(map.observer, {0,0,255,255});
    EndScissorMode();
}
} // namespace d2x
