#include "classic_hud.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
void imageAt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (image && image->texture.id)
        DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
                       bounds, {0, 0}, 0, tint);
}
}
void drawClassicGlobe(const GpuAnimation &orbs, const GpuAnimation &overlap, bool mana,
                      std::optional<float> fraction) {
    if (const auto *image = orbs.frame(0, mana ? 1 : 0); image && fraction) {
        const auto &texture = image->texture;
        const int filled = int(std::round(std::clamp(*fraction, 0.f, 1.f) * texture.height));
        if (filled > 0) {
            const float empty = float(texture.height - filled);
            auto bounds = hudGlobe(mana);
            bounds.y += empty * hudScale(); bounds.height = filled * hudScale();
            DrawTexturePro(texture, {0, empty, float(texture.width), float(filled)}, bounds, {0, 0}, 0, WHITE);
        }
    }
    imageAt(overlap.frame(0, mana ? 1 : 0), hudRect(mana ? 691 : 28, mana ? 96 : 93, 82, 88,
        mana ? HudAnchor::Right : HudAnchor::Left));
}
void drawClassicHud(const ClassicHudArt &art, const ClassicHudValues &value) {
    DrawRectangle(0, H - HUD, W, HUD, BLACK);
    // Frame 6's first 80 pixels contain the original empty bordered strip.
    // Tile its interior horizontally, preserving the artwork's pixel size.
    if (const auto *strip = art.panel.frame(0, 6); strip && W > 800) {
        const auto fill = [&](float left, float right) {
            for (float x = left; x < right; x += 78) {
                const float width = std::min(78.f, right - x);
                DrawTexturePro(strip->texture, {1, 0, width, float(strip->texture.height)},
                    {x, float(H - strip->texture.height), width, float(strip->texture.height)}, {}, 0, WHITE);
            }
        };
        fill(165, hudRect(165, 0, 0, 0).x);
        fill(hudRect(635, 0, 0, 0).x, hudSkillSlot(true).x);
    }
    for (bool mana : {false, true}) imageAt(art.orbs.frame(0, mana ? 1 : 0), hudGlobe(mana), BLACK);
    constexpr std::array<float, 6> offsets{0, 165, 293, 421, 549, 683};
    for (size_t i = 0; i < offsets.size(); ++i) {
        const auto &part = art.panel.frames.at(i);
        const auto anchor = i == 0 ? HudAnchor::Left : i == 5 ? HudAnchor::Right : HudAnchor::Center;
        imageAt(&part, hudRect(offsets[i], float(part.texture.height), float(part.texture.width), float(part.texture.height), anchor));
    }
    drawClassicGlobe(art.orbs, art.overlap, false, value.life);
    drawClassicGlobe(art.orbs, art.overlap, true, value.mana);
    if (value.stamina) {
        auto bounds = hudStamina(); const auto fraction = std::clamp(*value.stamina, 0.f, 1.f);
        bounds.width *= fraction;
        // Existing OpenDiablo2 color reference; original 1.13c blue is still an adaptation.
        const Color color = value.blueStamina ? Color{105, 105, 255, 200}
            : fraction < .25f ? Color{255, 0, 0, 200} : Color{175, 136, 72, 200};
        DrawRectangleRec(bounds, color);
    }
    if (value.experience) {
        auto bounds = hudExperience(); bounds.width *= std::clamp(*value.experience, 0.f, 1.f);
        bounds.height = 2 * hudScale(); DrawRectangleRec(bounds, WHITE);
    }
    imageAt(art.run.frame(0, value.running ? 2 : 0), hudRunButton());
    imageAt(art.points.frame(0, value.attributePoints ? (value.pressedPoint == false ? 1 : 0) : 2), hudRect(206, 38, 30, 30));
    imageAt(art.points.frame(0, value.skillPoints ? (value.pressedPoint == true ? 1 : 0) : 2), hudRect(563, 38, 30, 30));
    if (const auto *image = art.menu.frame(0, (value.miniPanel ? 2 : 0) + int(value.menuPressed)))
        imageAt(image, hudMenuButtonArt(*image));
}
} // namespace d2x
