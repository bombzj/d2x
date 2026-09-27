#include "primitives.hpp"
#include <algorithm>
#include <rlgl.h>
namespace d2x {
int UiPainter::measure(const std::string &text, int size) const {
    if (font.ready) {
        float width = 0;
        for (unsigned char c : text)
            width += font.widths[c] + 1;
        return int(width * size / 16.f);
    }
    return MeasureText(text.c_str(), size);
}
int direction(Vec look, int count) {
    auto s = project(look);
    float a = std::atan2(s.y, s.x);
    if (a < 0)
        a += 2 * pi;
    static constexpr int dir16[] = {7, 14, 3, 15, 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13};
    static constexpr int dir8[] = {7, 3, 4, 0, 5, 1, 6, 2};
    // The DCC direction order is interleaved; missiles such as Arrow use 32 directions.
    static constexpr int dir32[] = {7, 28, 14, 29, 3, 30, 15, 31, 4, 16, 8, 17, 0, 18, 9, 19,
                                    5, 20, 10, 21, 1, 22, 11, 23, 6, 24, 12, 25, 2, 26, 13, 27};
    if (count == 32)
        return dir32[int(std::round(a / (2 * pi) * 32)) % 32];
    if (count == 16)
        return dir16[int(std::round(a / (2 * pi) * 16)) % 16];
    if (count == 8)
        return dir8[int(std::round(a / (2 * pi) * 8)) % 8];
    return int(std::round(a / (2 * pi) * count)) % std::max(1, count);
}
void sprite(const Sprite *s, Vec p, Color tint) {
    if (!s || !s->texture.id)
        return;
    if (!s->layers.empty()) {
        for (const auto &layer : s->layers) {
            if (layer.softAdditive) {
                // Diablerie Materials.SoftAdditive: OneMinusDstColor, One.
                rlSetBlendFactors(0x0307, 1, 0x8006);
                BeginBlendMode(BLEND_CUSTOM);
            } else BeginBlendMode(BLEND_ALPHA);
            DrawTexture(layer.texture, int(p.x + layer.x), int(p.y + layer.y), tint);
            EndBlendMode();
        }
        return;
    }
    DrawTexture(s->texture, int(p.x + s->x), int(p.y + s->y), tint);
}
void softAdditiveSprite(const Sprite *s, Vec p, Color tint) {
    rlSetBlendFactors(0x0307, 1, 0x8006);
    BeginBlendMode(BLEND_CUSTOM);
    sprite(s, p, tint);
    EndBlendMode();
}
void spriteShadow(const Sprite *s, Vec p) {
    if (s && s->shadowTexture.id)
        DrawTexture(s->shadowTexture, int(p.x + s->shadowX), int(p.y + s->shadowY), {0, 0, 0, 191});
}
void UiPainter::label(const std::string &text, int x, int y, int size, Color c) const {
    if (font.ready) {
        float cursor = float(x), scale = size / 16.f;
        for (unsigned char ch : text) {
            auto s = font.glyphs.frame(0, font.indices[ch]);
            if (s && ch != ' ')
                DrawTexturePro(s->texture, {0, 0, float(s->texture.width), float(s->texture.height)},
                               {cursor, float(y), s->texture.width * scale, s->texture.height * scale},
                               {0, 0}, 0, c);
            cursor += (font.widths[ch] + 1) * scale;
        }
        return;
    }
    DrawText(text.c_str(), x + 1, y + 1, size, {0, 0, 0, 220});
    DrawText(text.c_str(), x, y, size, c);
}
void UiPainter::centered(const std::string &text, int y, int size, Color c) const {
    label(text, (W - measure(text, size)) / 2, y, size, c);
}
void UiPainter::inBox(const std::string &text, Rectangle bounds, int size, Color color) const {
    float top = 0, bottom = float(size);
    bool found = false;
    if (font.ready)
        for (unsigned char ch : text) {
            const auto *glyph = font.glyphs.frame(0, font.indices[ch]);
            if (!glyph || ch == ' ' || !glyph->hitHeight) continue;
            const float start = (glyph->hitY - glyph->y) * size / 16.f;
            const float end = start + glyph->hitHeight * size / 16.f;
            top = found ? std::min(top, start) : start;
            bottom = found ? std::max(bottom, end) : end;
            found = true;
        }
    label(text, int(bounds.x + (bounds.width - measure(text, size)) / 2),
          int(bounds.y + (bounds.height - (bottom - top)) / 2 - top), size, color);
}
void frame(Rectangle r, Color border) {
    DrawRectangleRec(r, {15, 15, 14, 238});
    DrawRectangleLinesEx(r, 1, {76, 67, 48, 255});
    DrawRectangleLinesEx({r.x + 3, r.y + 3, r.width - 6, r.height - 6}, 1, border);
}
void diamond(Vec p, float radius, Color c) {
    DrawLineV(rv(p + Vec{0, -radius * .5f}), rv(p + Vec{radius, 0}), c);
    DrawLineV(rv(p + Vec{radius, 0}), rv(p + Vec{0, radius * .5f}), c);
    DrawLineV(rv(p + Vec{0, radius * .5f}), rv(p + Vec{-radius, 0}), c);
    DrawLineV(rv(p + Vec{-radius, 0}), rv(p + Vec{0, -radius * .5f}), c);
}
} // namespace d2x
