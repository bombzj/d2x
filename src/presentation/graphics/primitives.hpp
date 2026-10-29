#pragma once
#include "core/math.hpp"
#include "graphics.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>
namespace d2x {
inline int W = 1066, H = 680;
inline constexpr int HUD = 74;
inline constexpr float pi = std::numbers::pi_v<float>;
// The scene renders into a window-sized target; UI coordinates stay in the
// virtual W x H space. Normal frames scale by viewport.scale and uiScale stays
// uniform for text metrics. The three fixed 800x600 screens (title, character
// list, NPC trade) draw into a sub-canvas stretched across the window, so
// scissors need separate horizontal/vertical layout -> pixel factors
// (uiScaleX/uiScaleY) matching the rlScalef used for that canvas.
inline float uiScale = 1.f, uiScaleX = 1.f, uiScaleY = 1.f;
inline void uiScissor(float x, float y, float width, float height) {
    const int left = int(x * uiScaleX + 0.5f), top = int(y * uiScaleY + 0.5f);
    const int right = int((x + width) * uiScaleX + 0.5f);
    const int bottom = int((y + height) * uiScaleY + 0.5f);
    BeginScissorMode(left, top, right - left, bottom - top);
}
inline constexpr Color gold{196, 164, 102, 255}, parchment{209, 196, 162, 255};
inline Vector2 rv(Vec v) {
    return {v.x, v.y};
}
// Original font .tbl maps a Unicode code point to a DC6 glyph frame. Latin
// tables cover 0..255; CJK tables cover thousands of sorted code points. Glyph
// textures are uploaded on first use: a CJK font can hold over 16k frames, so
// uploading every frame up front is slow and memory heavy.
struct ClassicFont {
    Graphics *graphics = nullptr;   // owns lazily uploaded glyph textures
    const Animation *source = nullptr; // decoded DC6 frames
    std::array<uint8_t, 256> transform{}; // optional PL2 index transform
    bool transformed = false;
    Color textColor = WHITE; // PL2 font color for the outline-font backend
    std::vector<uint32_t> codes;  // sorted code points
    std::vector<uint16_t> frames; // parallel DC6 frame
    std::vector<uint8_t> widths;  // parallel advance width
    mutable std::vector<Sprite> sprites; // uploaded frames, indexed by DC6 frame
    mutable std::vector<uint8_t> uploaded;
    bool ready = false;

    int indexOf(uint32_t code) const {
        const auto it = std::lower_bound(codes.begin(), codes.end(), code);
        return it != codes.end() && *it == code ? int(it - codes.begin()) : -1;
    }
    int widthOf(uint32_t code) const {
        const int i = indexOf(code);
        return i < 0 ? 0 : widths[size_t(i)];
    }
    const Sprite *glyphFrame(uint16_t frame) const;
    const Sprite *glyphOf(uint32_t code) const {
        const int i = indexOf(code);
        return i < 0 ? nullptr : glyphFrame(frames[size_t(i)]);
    }
};
// Populate a ClassicFont from an original "Woo!" font table. Returns false when
// the table is too small or holds no records.
bool parseClassicFontTable(std::string_view tbl, ClassicFont &font);
// Reads an original DC6 font set and its codepoint table into ClassicFont.
// Extracted so a future TTF backend can populate a font through one entry point
// instead of the DC6 loader.
void loadClassicFont(Graphics &graphics, Archives &archives, ClassicFont &font,
                     std::string_view name, int color = 0);
// Sky PL2 stores its thirteen font text colours as three bytes each just before
// the index transforms. The outline-font backend tints with this RGB, so a
// colored ClassicFont needs it alongside the DC6 transform.
Color pl2FontColor(const Bytes &palette, int color);
// Rasterizes one decoded DC6 glyph frame into a GPU sprite. Kept separate from
// text layout so a TTF backend can supply glyphs without DC6 source frames.
Sprite rasterizeGlyph(Graphics &graphics, const IndexedFrame &frame, const ClassicFont &font);
// Shared TTF text backend. When a CJK outline font is available, text is drawn
// from it instead of the upscaled DC6 glyph atlas. The atlas is built lazily
// from the code points the DC6 font tables report, so only glyphs the game can
// show are rasterized.
void collectFontCodepoints(const std::vector<uint32_t> &codes);
void setTtfSearchDirectory(std::string_view directory);
bool ttfTextReady();
Vector2 ttfMeasure(std::string_view text, float size, float spacing);
void ttfDraw(std::string_view text, float x, float y, float size, float spacing, Color color);
class UiPainter {
    const ClassicFont &font;
    int glyphGap;

  public:
    explicit UiPainter(const ClassicFont &f, int gap = 1) : font(f), glyphGap(gap) {}
    int measure(const std::string &text, int size) const;
    void label(const std::string &text, int x, int y, int size, Color color = parchment) const;
    void centered(const std::string &text, int y, int size, Color color = parchment) const;
    void inBox(const std::string &text, Rectangle bounds, int size, Color color = parchment) const;
};
// World-space heading to the original interleaved DCC direction index.
int direction(Vec look, int count);
void sprite(const Sprite *sprite, Vec position, Color tint = WHITE);
inline bool selectionRectHit(Rectangle bounds, Vec position, Vec mouse) {
    const auto point = mouse - position;
    return bounds.width >= 0 && bounds.height >= 0 && point.x >= bounds.x && point.y >= bounds.y &&
        point.x <= bounds.x + bounds.width && point.y <= bounds.y + bounds.height;
}
// Retail 1.13c D2Client A68E0 / A6730: COF broad phase, then selectable
// component frames with 12-pixel padding and symmetric minimum-32 growth.
// Single-piece art (including ground items) uses its entire original frame.
inline bool spriteHit(const Sprite *image, Vec position, Vec mouse, bool multiPiece = true,
                      std::optional<Vec> coarsePosition = {}) {
    if (!image) return false;
    if (image->selectionCoarseBounds) {
        const auto point = mouse - coarsePosition.value_or(position);
        if (!CheckCollisionPointRec(rv(point), *image->selectionCoarseBounds)) return false;
    }
    const auto hit = [&](Rectangle bounds) {
        bounds.x -= 12; bounds.y -= 12; bounds.width += 24; bounds.height += 24;
        if (bounds.width < 32) {const int grow = (32 - int(bounds.width)) / 2; bounds.x -= grow; bounds.width += 2 * grow;}
        if (bounds.height < 32) {const int grow = (32 - int(bounds.height)) / 2; bounds.y -= grow; bounds.height += 2 * grow;}
        return selectionRectHit(bounds, position, mouse);
    };
    if (!image->componentBounds.empty()) {
        for (const auto &component : image->componentBounds)
            if ((!multiPiece || component.selectable) && hit(component.bounds)) return true;
        return false;
    }
    return image->texture.width > 0 && image->texture.height > 0 &&
        hit({float(image->x),float(image->y),float(image->texture.width),float(image->texture.height)});
}
// Cursor hotspots are texture-local, measured from the top-left pixel.
Vec handCursorHotspot(const Sprite *sprite);
void cursorSprite(const Sprite *sprite, Vec mouse, Vec hotspot);
void softAdditiveSprite(const Sprite *sprite, Vec position, Color tint = WHITE);
void spriteShadow(const Sprite *sprite, Vec position);
void frame(Rectangle bounds, Color border = gold);
// Current MPQ boxpieces frame layout; original pixels, scaled without a drawn substitute.
void originalBox(const GpuAnimation &pieces, Rectangle bounds, float scale);
void diamond(Vec position, float radius, Color color);
} // namespace d2x
