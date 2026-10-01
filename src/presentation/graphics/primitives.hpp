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
// Cursor hotspots are texture-local, measured from the top-left pixel.
Vec handCursorHotspot(const Sprite *sprite);
void cursorSprite(const Sprite *sprite, Vec mouse, Vec hotspot);
void softAdditiveSprite(const Sprite *sprite, Vec position, Color tint = WHITE);
void spriteShadow(const Sprite *sprite, Vec position);
void frame(Rectangle bounds, Color border = gold);
void diamond(Vec position, float radius, Color color);
inline constexpr int worldPageSize = 8;
inline Rectangle travelSlot(int i) {
    return {W / 2.f - 325, 144.f + i * 49, 650, 44};
}
inline Rectangle travelPageButton(bool next) {
    return {W / 2.f + (next ? 200.f : -325.f), 546, 125, 30};
}
} // namespace d2x
