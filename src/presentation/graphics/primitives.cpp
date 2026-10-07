#include "primitives.hpp"
#include <algorithm>
#include <rlgl.h>
namespace d2x {
void originalBox(const GpuAnimation &pieces, Rectangle bounds, float scale) {
    if (pieces.count < 22 || bounds.width <= 0 || bounds.height <= 0) return;
    const auto *corner = pieces.frame(0, 0);
    const float w = corner->texture.width * scale, h = corner->texture.height * scale;
    auto piece = [&](int index, float x, float y) {
        const auto *image = pieces.frame(0, index);
        DrawTexturePro(image->texture, {0, 0, float(image->texture.width), float(image->texture.height)},
            {x, y, image->texture.width * scale, image->texture.height * scale}, {0, 0}, 0, WHITE);
    };
    // Side ink is at x=5..7; corner ink is x=1..3 / x=10..12.
    // Bottom edge frames have top-edge ink, so offset them by nine pixels to
    // join the bottom corners. Derived from the current 14x15 MPQ frames.
    const float verticalStep = h - 5 * scale;
    BeginScissorMode(int(bounds.x), int(bounds.y+4*scale), int(bounds.width), int(bounds.height-9*scale));
    for (int index = 0; index * verticalStep < bounds.height; ++index) {
        piece(10 + index % 3, bounds.x - 4 * scale, bounds.y + index * verticalStep);
        piece(13 + index % 3, bounds.x + bounds.width - w + 5 * scale, bounds.y + index * verticalStep);
    }
    EndScissorMode();
    BeginScissorMode(int(bounds.x), int(bounds.y), int(bounds.width), int(bounds.height));
    const float horizontalStep = (corner->texture.width - 2) * scale;
    for (int index = 0; index * horizontalStep < bounds.width; ++index) {
        piece(2 + index % 6, bounds.x + index * horizontalStep, bounds.y);
        piece(16 + index % 6, bounds.x + index * horizontalStep, bounds.y + bounds.height - h + 9 * scale);
    }
    piece(0, bounds.x, bounds.y); piece(1, bounds.x + bounds.width - w, bounds.y);
    piece(8, bounds.x, bounds.y + bounds.height - h);
    piece(9, bounds.x + bounds.width - w, bounds.y + bounds.height - h);
    EndScissorMode();
}
int UiPainter::measure(const std::string &text, int size) const {
    if (font.ready) {
        float width = 0;
        for (unsigned char c : text)
            width += font.widths[c] + glyphGap;
        return int(width * size / 16.f);
    }
    return MeasureText(text.c_str(), size);
}
int direction(Vec look, int count) {
    // Diablerie Iso.Direction quantizes world angles; DCC art is already projected.
    // Rotate to the east-first mapping below without project's vertical compression.
    float a = std::atan2(look.x + look.y, look.x - look.y);
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
Vec handCursorHotspot(const Sprite *s) {
    if (!s) return {};
    // OpenDiablo2 GuiManager.renderCursor: mouse + DC6 offset - frame height.
    // Convert ohand's bottom-origin placement to a texture-local hotspot once.
    return {-float(s->x), float(s->texture.height - s->y)};
}
void cursorSprite(const Sprite *s, Vec mouse, Vec hotspot) {
    if (!s || !s->texture.id) return;
    // Diablerie SoftwareCursor.SetCursor uses an explicit top-left-relative hotspot.
    // Raw DC6 offsets describe drawing origins, not a universal cursor hotspot.
    DrawTexture(s->texture, int(mouse.x - hotspot.x), int(mouse.y - hotspot.y), WHITE);
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
            cursor += (font.widths[ch] + glyphGap) * scale;
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
