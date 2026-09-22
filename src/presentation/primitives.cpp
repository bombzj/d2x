#include "primitives.hpp"
#include <algorithm>
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
    static constexpr int dir16[] = {7, 15, 3, 11, 4, 8, 0, 12, 5, 9, 1, 13, 6, 10, 2, 14};
    static constexpr int dir8[] = {7, 3, 4, 0, 5, 1, 6, 2};
    if (count == 16)
        return dir16[int(std::round(a / (2 * pi) * 16)) % 16];
    if (count == 8)
        return dir8[int(std::round(a / (2 * pi) * 8)) % 8];
    return int(std::round(a / (2 * pi) * count)) % std::max(1, count);
}
void sprite(const Sprite *s, Vec p, Color tint) {
    if (!s || !s->texture.id)
        return;
    DrawTexture(s->texture, int(p.x + s->x), int(p.y + s->y), tint);
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
void icon(int id, Vec p, bool hot, float t) {
    auto c = hot ? WHITE : gold;
    if (id == 0) {
        for (int i = 7; i >= 0; i--)
            DrawCircle(int(p.x - i * 2), int(p.y + i), float(2 + i * .3f),
                       {uint8_t(230 - i * 12), uint8_t(55 + i * 7), 15, 255});
        DrawCircleV(rv(p), 6, {255, 184, 75, 255});
        DrawCircleV(rv(p), 2, WHITE);
    }
    if (id == 1) {
        for (int i = 0; i < 8; i++) {
            float a = i * pi / 4;
            Vec v{std::cos(a), std::sin(a)};
            DrawLineEx(rv(p + v * 3), rv(p + v * 13), 2, {119, 203, 248, 255});
        }
        DrawCircleLines(int(p.x), int(p.y), 6, {196, 231, 255, 255});
    }
    if (id == 2) {
        for (int i = 0; i < 3; i++)
            DrawRing(rv(p), 6.f + i * 3, 7.f + i * 3, i * 115 + int(t * 40) % 20,
                     i * 115 + 200 + int(t * 40) % 20, 15, c);
    }
    if (id == 3) {
        DrawEllipseLines(int(p.x), int(p.y), 8, 14, {126, 174, 255, 255});
        DrawEllipseLines(int(p.x), int(p.y), 5, 10, {199, 224, 255, 255});
    }
    if (id == 4) {
        DrawLineEx(rv(p + Vec{-12, 10}), rv(p + Vec{0, -10}), 3, gold);
        DrawLineEx(rv(p + Vec{0, -10}), rv(p + Vec{12, 10}), 3, gold);
        DrawCircleV(rv(p + Vec{0, -10}), 3, WHITE);
    }
    if (id == 5) {
        for (int i = 0; i < 3; i++)
            DrawRing(rv(p), float(3 + i * 4), float(4 + i * 4), -65, 65, 16, {213, 168, 80, 255});
        DrawCircleV(rv(p + Vec{-5, 0}), 4, gold);
    }
}

} // namespace d2x
