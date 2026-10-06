#pragma once
#include "core/math.hpp"
#include "graphics.hpp"
#include <numbers>
namespace d2x {
inline constexpr int W = 1066, H = 680, HUD = 74;
inline constexpr float pi = std::numbers::pi_v<float>;
inline constexpr Color gold{196, 164, 102, 255}, parchment{209, 196, 162, 255};
inline Vector2 rv(Vec v) {
    return {v.x, v.y};
}
struct ClassicFont {
    GpuAnimation glyphs;
    std::array<int, 256> widths{}, indices{};
    bool ready = false;
};
class UiPainter {
    const ClassicFont &font;

  public:
    explicit UiPainter(const ClassicFont &f) : font(f) {}
    int measure(const std::string &text, int size) const;
    void label(const std::string &text, int x, int y, int size, Color color = parchment) const;
    void centered(const std::string &text, int y, int size, Color color = parchment) const;
    void inBox(const std::string &text, Rectangle bounds, int size, Color color = parchment) const;
};
// World-space heading to the original interleaved DCC direction index.
int direction(Vec look, int count);
void sprite(const Sprite *sprite, Vec position, Color tint = WHITE);
// Use the opaque bounds of the original frame for both local and remote selection.
inline bool spriteHit(const Sprite *image, Vec position, Vec mouse) {
    return image && image->hitWidth > 0 && image->hitHeight > 0 &&
        CheckCollisionPointRec(rv(mouse), {position.x + image->hitX, position.y + image->hitY,
            float(image->hitWidth), float(image->hitHeight)});
}
// Cursor hotspots are texture-local, measured from the top-left pixel.
Vec handCursorHotspot(const Sprite *sprite);
void cursorSprite(const Sprite *sprite, Vec mouse, Vec hotspot);
void softAdditiveSprite(const Sprite *sprite, Vec position, Color tint = WHITE);
void spriteShadow(const Sprite *sprite, Vec position);
void frame(Rectangle bounds, Color border = gold);
void diamond(Vec position, float radius, Color color);
} // namespace d2x
