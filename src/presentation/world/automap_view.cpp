#include "presentation/scene_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
void SceneView::drawAutomap(const AutomapDrawView &map) const {
    if (!view_.automap) return;
    const bool large = view_.automapLarge;
    const auto viewport = worldViewport();
    // Keep the map inside the visible world when a side panel is open.
    const float width = std::min(240.f, viewport.width - 24);
    const Rectangle area = large ? viewport
        : Rectangle{view_.minimapRight ? viewport.x + viewport.width - width - 12 : viewport.x + 12,
                    38, width, 175};
    const Vec center{area.x + area.width * .5f, area.y + area.height * .5f};
    const float scale = large ? .1f : .05f;
    const Vec origin = project(map.observer);
    auto onMap = [&](Vec world) {
        const auto position = (project(world) - origin) * scale + center + view_.automapOffset;
        return Vec{std::round(position.x), std::round(position.y)};
    };
    // D2Client 1.13c 0x5F1C0 / 0xD2DE8: twelve connected original
    // automap line segments. These unit marks are not DC6 cells 221/317.
    auto unitMark = [&](Vec world, Color rgb) {
        constexpr std::array<Vec, 13> points{{{0,-1}, {2,-2}, {4,-1}, {2,0}, {4,1}, {2,2},
            {0,1}, {-2,2}, {-4,1}, {-2,0}, {-4,-1}, {-2,-2}, {0,-1}}};
        auto at = onMap(world) + Vec{8, -8};
        if (!large) at = at + Vec{-1, 5};
        const auto color = assets_.automapColor(rgb);
        for (size_t i = 1; i < points.size(); ++i)
            DrawLineV(rv(at + points[i - 1] * 2), rv(at + points[i] * 2), color);
    };
    const Rectangle fadedCenter{area.x + area.width * .25f, area.y + area.height * .25f,
        area.width * .5f, area.height * .5f};
    auto mapSprite = [&](const Sprite &image, Vec position, bool npc = false) {
        if (npc || view_.automapFade == AutomapFade::No) {
            sprite(&image, position, WHITE);
        } else if (view_.automapFade != AutomapFade::Center || !large) {
            sprite(&image, position, {255, 255, 255, 128});
        } else {
            const float left = position.x + image.x, top = position.y + image.y;
            auto clipped = [&](Rectangle clip, uint8_t alpha) {
                const float clipLeft = std::max(left, clip.x), clipTop = std::max(top, clip.y);
                const float clipRight = std::min(left + image.texture.width, clip.x + clip.width);
                const float clipBottom = std::min(top + image.texture.height, clip.y + clip.height);
                if (clipRight <= clipLeft || clipBottom <= clipTop) return;
                const Rectangle source{clipLeft - left, clipTop - top,
                    clipRight - clipLeft, clipBottom - clipTop};
                DrawTexturePro(image.texture, source,
                    {clipLeft, clipTop, source.width, source.height}, {0, 0}, 0, {255, 255, 255, alpha});
            };
            clipped({area.x, area.y, area.width, fadedCenter.y - area.y}, 255);
            clipped({area.x, fadedCenter.y + fadedCenter.height, area.width,
                area.y + area.height - fadedCenter.y - fadedCenter.height}, 255);
            clipped({area.x, fadedCenter.y, fadedCenter.x - area.x, fadedCenter.height}, 255);
            clipped({fadedCenter.x + fadedCenter.width, fadedCenter.y,
                area.x + area.width - fadedCenter.x - fadedCenter.width, fadedCenter.height}, 255);
            clipped(fadedCenter, 128);
        }
    };
    BeginScissorMode(int(area.x), int(area.y), int(area.width), int(area.height));
    BeginBlendMode(BLEND_ALPHA);
    for (const auto &town : map.towns)
        for (const auto &image : assets_.townAutomapSprites(town.level, town.variant, large))
            mapSprite(image, onMap(town.center));
    for (const auto &stamp : map.stamps)
        if (const auto *image = assets_.automapSprite(stamp.cel, large))
            mapSprite(*image, onMap(stamp.position));
    for (const auto &marker : map.markers) {
        using Mark = AutomapDrawView::UnitMark;
        if ((marker.unitMark == Mark::Party || marker.unitMark == Mark::PartyPet) && !view_.automapParty) continue;
        if (const auto *image = assets_.automapSprite(marker.cel, large))
            mapSprite(*image, onMap(marker.position), marker.npc);
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
        default: break;
        }
    }
    EndBlendMode();
    if (view_.automapNames) for (const auto &marker : map.markers) {
        if (!marker.showName) continue;
        using Mark=AutomapDrawView::UnitMark;
        if ((marker.unitMark == Mark::Party || marker.unitMark == Mark::OtherPlayer || marker.unitMark==Mark::Corpse) && !view_.automapParty) continue;
        auto position = onMap(marker.position) + Vec{8,-8};
        if (!large) position = position + Vec{-1,5};
        const auto &font = marker.unitMark == AutomapDrawView::UnitMark::Party ? assets_.automapPartyFont :
            marker.unitMark==Mark::OtherPlayer || marker.unitMark==Mark::Corpse ? assets_.automapOtherFont :
            marker.npc ? assets_.automapNpcFont : assets_.characterLabelFont;
        int width = 0;
        for (unsigned char c : marker.name) width += font.widths[c];
        float x = position.x - width / 2;
        for (unsigned char c : marker.name) {
            if (const auto *glyph = font.glyphs.frame(0, font.indices[c]); glyph && c != ' ')
                DrawTexture(glyph->texture, int(x + glyph->x), int(position.y - 10 + glyph->y - glyph->texture.height), WHITE);
            x += font.widths[c];
        }
    }
    if(map.observerVisible) unitMark(map.observer, {0,0,255,255});
    EndScissorMode();
}
} // namespace d2x
