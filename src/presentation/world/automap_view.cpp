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
    for (const auto &marker : map.markers)
        if (const auto *image = assets_.automapSprite(marker.cel, large))
            mapSprite(*image, onMap(marker.position), marker.npc);
    EndBlendMode();
    if (view_.automapNames) for (const auto &marker : map.markers) {
        if (!marker.showName) continue;
        const auto position = onMap(marker.position);
        painter_.label(marker.name, int(position.x - painter_.measure(marker.name, 10) / 2),
            int(position.y - 20), 10, marker.npc ? gold : WHITE);
    }
    const auto player = onMap(map.observer);
    DrawCircleV(rv(player), large ? 2.f : 1.f, WHITE);
    DrawLineV(rv(player + Vec{-5, 0}), rv(player + Vec{5, 0}), WHITE);
    DrawLineV(rv(player + Vec{0, -4}), rv(player + Vec{0, 4}), WHITE);
    EndScissorMode();
}
} // namespace d2x
