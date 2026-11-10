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
extern int W, H;
// The 55-pixel middle cells of 800ctrlpnl7.dc6 have seven rows above
// the solid strip. Render the world behind their transparent pixels.
// Artwork retains its native size; the centered 800-pixel strip blocks only its bottom 48 pixels.
inline constexpr int HUD = 48;
inline constexpr float pi = std::numbers::pi_v<float>;
// The scene renders into a window-sized target; UI coordinates stay in the
// virtual W x H space. Normal frames scale by viewport.scale and uiScale stays
// uniform for text metrics. The three fixed 800x600 screens (title, character
// list, NPC trade) draw into a sub-canvas stretched across the window, so
// scissors need separate horizontal/vertical layout -> pixel factors
// (uiScaleX/uiScaleY) matching the rlScalef used for that canvas.
extern float uiScale, uiScaleX, uiScaleY;
void uiScissor(float x, float y, float width, float height);
inline constexpr Color gold{196, 164, 102, 255}, parchment{209, 196, 162, 255};
Vector2 rv(Vec v);
// Original font .tbl maps a Unicode code point to a DC6 glyph frame. Latin
// tables cover 0..255; CJK tables cover thousands of sorted code points. Glyph
// textures are uploaded on first use: a CJK font can hold over 16k frames, so
// uploading every frame up front is slow and memory heavy.
struct ClassicFont {
    Graphics *graphics = nullptr;   // owns lazily uploaded glyph textures
    const Animation *source = nullptr; // decoded DC6 frames
    std::array<uint8_t, 256> transform{}; // optional PL2 index transform
    bool transformed = false;
    bool originalGlyphs = false; // Resource-specific pages may require the native DC6 face.
    Color textColor = WHITE; // PL2 font color for the outline-font backend
    int textHeight = 0; // Woo! header metric used by D2Win control baselines.
    int framedLineHeight = 0, framedBaselineOffset = 0;
    std::vector<uint32_t> codes;  // sorted code points
    std::vector<uint16_t> frames; // parallel DC6 frame
    std::vector<uint8_t> widths;  // parallel advance width
    mutable std::map<uint16_t, Sprite> sprites; // Only glyphs used in this color variant.
    bool ready = false;

    int indexOf(uint32_t code) const;
    int widthOf(uint32_t code) const;
    int textWidth(std::string_view line) const;
    void drawBaseline(std::string_view line, Vec at, float scale, Color tint = WHITE) const;
    void drawBaselineRows(std::string_view line, Vec at, int skipBottom, int rows) const;
    const Sprite *glyphFrame(uint16_t frame) const;
    const Sprite *glyphOf(uint32_t code) const;
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
// from it. The atlas includes printable ASCII and the non-ASCII glyphs used by text.
void setTtfSearchDirectory(std::string_view directory);
bool ttfTextReady();
Vector2 ttfMeasure(std::string_view text, float size, float spacing);
void ttfDraw(std::string_view text, float x, float y, float size, float spacing, Color color);
class UiPainter {
    const ClassicFont &font;
    int glyphGap;

  public:
    explicit UiPainter(const ClassicFont &f, int gap = 1);
    int measure(const std::string &text, int size) const;
    void label(const std::string &text, int x, int y, int size, Color color = parchment) const;
    void centered(const std::string &text, int y, int size, Color color = parchment) const;
    void inBox(const std::string &text, Rectangle bounds, int size, Color color = parchment) const;
};
// World-space heading to the original interleaved DCC direction index.
int direction(Vec look, int count);
void sprite(const Sprite *sprite, Vec position, Color tint = WHITE);
bool selectionRectHit(Rectangle bounds, Vec position, Vec mouse);
// Retail 1.13c D2Client A68E0 / A6730: COF broad phase, then selectable
// component frames with 12-pixel padding and symmetric minimum-32 growth.
// Single-piece art (including ground items) uses its entire original frame.
bool spriteHit(const Sprite *image, Vec position, Vec mouse, bool multiPiece = true,
               std::optional<Vec> coarsePosition = {});
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
