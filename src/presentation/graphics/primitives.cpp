#include "primitives.hpp"
#include "core/text.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <rlgl.h>
#include <stdexcept>
namespace d2x {
namespace {
std::vector<uint32_t> &collectedCodepoints() { static std::vector<uint32_t> value; return value; }
std::string &ttfSearchDirectory() { static std::string value; return value; }
Font &sharedTtf() { static Font value{}; return value; }
bool &sharedTtfBuilt() { static bool value = false; return value; }
bool &sharedTtfFailed() { static bool value = false; return value; }
bool &ttfDirty() { static bool value = false; return value; }
int &ttfBaseSize() { static int value = 0; return value; }
std::vector<std::string> ttfCandidates() {
    std::vector<std::string> paths;
    const auto &gameDirectory = ttfSearchDirectory();
    if (!gameDirectory.empty()) {
        paths.push_back(gameDirectory + "/fonts/simsun.ttc");
        paths.push_back(gameDirectory + "/simsun.ttc");
    }
    paths.emplace_back("fonts/simsun.ttc");
    paths.emplace_back(std::string(GetApplicationDirectory()) + "fonts/simsun.ttc");
    paths.emplace_back(std::string(GetWorkingDirectory()) + "/fonts/simsun.ttc");
    paths.emplace_back("/usr/share/fonts/truetype/winfonts/simsun.ttc");
    paths.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    paths.emplace_back("C:/Windows/Fonts/simsun.ttc");
    paths.emplace_back("C:/Windows/Fonts/msyh.ttc");
    return paths;
}
uint32_t readBe32(const unsigned char *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
bool extractTtcFace(const unsigned char *file, int size, std::vector<unsigned char> &out) {
    const uint32_t off = readBe32(file + 12);
    if (off + 12 > uint32_t(size)) return false;
    const uint16_t numTables = uint16_t((file[off + 4] << 8) | file[off + 5]);
    if (numTables == 0 || numTables > 4096) return false;
    const uint32_t dirBytes = 12u + 16u * numTables;
    if (off + dirBytes > uint32_t(size)) return false;
    out.assign(file + off, file + off + dirBytes);
    size_t cursor = (dirBytes + 3) & ~size_t(3);
    out.resize(cursor);
    for (uint16_t i = 0; i < numTables; ++i) {
        const uint32_t srcOff = readBe32(file + off + 12 + 16 * i + 8);
        const uint32_t len = readBe32(file + off + 12 + 16 * i + 12);
        if (srcOff + len > uint32_t(size)) return false;
        const size_t index = 12 + 16 * size_t(i) + 8;
        out[index] = cursor >> 24;
        out[index + 1] = cursor >> 16;
        out[index + 2] = cursor >> 8;
        out[index + 3] = cursor;
        out.insert(out.end(), file + srcOff, file + srcOff + len);
        cursor = (cursor + len + 3) & ~size_t(3);
        out.resize(cursor);
    }
    return true;
}
Font *buildSharedTtf() {
    const int base = std::max(1, int(std::lround(16.f * uiScale)));
    if (sharedTtfBuilt() && !ttfDirty() && ttfBaseSize() == base)
        return sharedTtfFailed() ? nullptr : &sharedTtf();
    if (sharedTtf().texture.id) UnloadFont(sharedTtf());
    sharedTtf() = Font{};
    sharedTtfBuilt() = true;
    sharedTtfFailed() = false;
    ttfDirty() = false;
    ttfBaseSize() = base;
    std::vector<uint32_t> codes = collectedCodepoints();
    for (uint32_t code = 32; code < 127; ++code) codes.push_back(code);
    for (uint32_t code = 0x00A0; code <= 0x00FF; ++code) codes.push_back(code);
    for (uint32_t code = 0x2000; code <= 0x206F; ++code) codes.push_back(code);
    for (uint32_t code = 0x3000; code <= 0x303F; ++code) codes.push_back(code);
    for (uint32_t code = 0x4E00; code <= 0x9FFF; ++code) codes.push_back(code);
    for (uint32_t code = 0xFF00; code <= 0xFFEF; ++code) codes.push_back(code);
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    codes.erase(std::remove_if(codes.begin(), codes.end(), [](uint32_t c) { return c < 32; }), codes.end());
    std::vector<int> points(codes.begin(), codes.end());
    for (const auto &path : ttfCandidates()) {
        int size = 0;
        auto *file = LoadFileData(path.c_str(), &size);
        if (!file) continue;
        const unsigned char *data = file;
        int dataSize = size;
        std::vector<unsigned char> face;
        if (size >= 16 && std::memcmp(file, "ttcf", 4) == 0) {
            if (!extractTtcFace(file, size, face)) { UnloadFileData(file); continue; }
            data = face.data();
            dataSize = int(face.size());
        }
        sharedTtf() = LoadFontFromMemory(".ttf", data, dataSize, base, points.data(), int(points.size()));
        UnloadFileData(file);
        bool recsOk = false;
        if (sharedTtf().recs && sharedTtf().glyphs) {
            for (int i = 0; i < sharedTtf().glyphCount; ++i)
                if (sharedTtf().glyphs[i].value == 33) { recsOk = sharedTtf().recs[i].width > 0; break; }
        }
        if (sharedTtf().texture.id && recsOk) {
            SetTextureFilter(sharedTtf().texture, TEXTURE_FILTER_BILINEAR);
            return &sharedTtf();
        }
        if (sharedTtf().texture.id) UnloadFont(sharedTtf());
        sharedTtf() = Font{};
    }
    sharedTtfFailed() = true;
    return nullptr;
}
}
void collectFontCodepoints(const std::vector<uint32_t> &codes) {
    auto &dst = collectedCodepoints();
    dst.insert(dst.end(), codes.begin(), codes.end());
    ttfDirty() = true;
}
void setTtfSearchDirectory(std::string_view directory) {
    if (!directory.empty()) ttfSearchDirectory().assign(directory.data(), directory.size());
}
bool ttfTextReady() { return buildSharedTtf() != nullptr; }
Vector2 ttfMeasure(std::string_view text, float size, float spacing) {
    Font *font = buildSharedTtf();
    if (!font) return {0, size};
    const std::string owned(text);
    return MeasureTextEx(*font, owned.c_str(), size, spacing);
}
void ttfDraw(std::string_view text, float x, float y, float size, float spacing, Color color) {
    Font *font = buildSharedTtf();
    if (!font) return;
    const std::string owned(text);
    DrawTextEx(*font, owned.c_str(), {x, y}, size, spacing, color);
}
bool parseClassicFontTable(std::string_view tbl, ClassicFont &font) {
    if (tbl.size() < 12) return false;
    const size_t count = (tbl.size() - 12) / 14;
    if (count == 0) return false;
    struct Record {
        uint32_t code;
        uint16_t frame;
        uint8_t width;
    };
    std::vector<Record> records;
    records.reserve(count);
    const auto *bytes = reinterpret_cast<const uint8_t *>(tbl.data());
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + 12 + i * 14;
        records.push_back({uint32_t(r[0]) | (uint32_t(r[1]) << 8),
                           uint16_t(r[8]) | (uint16_t(r[9]) << 8), r[3]});
    }
    std::stable_sort(records.begin(), records.end(),
                     [](const Record &a, const Record &b) { return a.code < b.code; });
    font.codes.clear();
    font.frames.clear();
    font.widths.clear();
    font.sprites.clear();
    font.uploaded.clear();
    font.codes.reserve(count);
    font.frames.reserve(count);
    font.widths.reserve(count);
    for (const auto &record : records) {
        font.codes.push_back(record.code);
        font.frames.push_back(record.frame);
        font.widths.push_back(record.width);
    }
    return true;
}
Color pl2FontColor(const Bytes &palette, int color) {
    const size_t rgb = 0x6B600 + size_t(color) * 3;
    if (color < 0 || color >= 13 || palette.size() < rgb + 3) return WHITE;
    return {palette[rgb], palette[rgb + 1], palette[rgb + 2], 255};
}
void loadClassicFont(Graphics &graphics, Archives &archives, ClassicFont &font,
                     std::string_view name, int color) {
    const auto path = "data/local/font/" + archives.languageFontDirectory + "/" + std::string(name);
    setTtfSearchDirectory(archives.gameDirectory.string());
    const auto *original = graphics.animation(path + ".dc6");
    if (!original)
        throw std::runtime_error("Original UI font is missing: " + path);
    font.graphics = &graphics;
    font.source = original;
    font.transformed = false;
    font.textColor = WHITE;
    if (color) {
        constexpr size_t fontShifts = 0x6B600 + 13 * 3;
        const auto palette = archives.read("data/global/palette/sky/pal.pl2");
        if (color < 0 || color >= 13 || palette.size() < fontShifts + 13 * 256)
            throw std::runtime_error("Original UI font color transform is missing: " + path);
        for (size_t i = 0; i < font.transform.size(); ++i)
            font.transform[i] = palette[fontShifts + size_t(color) * 256 + i];
        font.transformed = true;
        font.textColor = pl2FontColor(palette, color);
    }
    auto tbl = archives.read(path + ".tbl", false);
    if (original->frames.empty() ||
        !parseClassicFontTable(std::string_view(reinterpret_cast<const char *>(tbl.data()), tbl.size()), font))
        throw std::runtime_error("Original UI font is missing: " + path);
    collectFontCodepoints(font.codes);
    font.ready = true;
}
Sprite rasterizeGlyph(Graphics &graphics, const IndexedFrame &frame, const ClassicFont &font) {
    if (font.transformed) {
        auto glyph = frame;
        for (auto &pixel : glyph.pixels)
            if (pixel) pixel = font.transform[pixel];
        return graphics.upload(glyph);
    }
    return graphics.upload(frame);
}
const Sprite *ClassicFont::glyphFrame(uint16_t frame) const {
    if (!graphics || !source || frame >= source->frames.size()) return nullptr;
    if (sprites.size() != source->frames.size()) {
        sprites.resize(source->frames.size());
        uploaded.assign(source->frames.size(), 0);
    }
    if (!uploaded[frame]) {
        sprites[frame] = rasterizeGlyph(*graphics, source->frames[frame], *this);
        uploaded[frame] = 1;
    }
    return &sprites[frame];
}
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
    uiScissor(bounds.x, bounds.y+4*scale, bounds.width, bounds.height-9*scale);
    for (int index = 0; index * verticalStep < bounds.height; ++index) {
        piece(10 + index % 3, bounds.x - 4 * scale, bounds.y + index * verticalStep);
        piece(13 + index % 3, bounds.x + bounds.width - w + 5 * scale, bounds.y + index * verticalStep);
    }
    EndScissorMode();
    uiScissor(bounds.x, bounds.y, bounds.width, bounds.height);
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
    if (ttfTextReady())
        return int(ttfMeasure(text, float(size), float(glyphGap)).x + 0.5f);
    if (font.ready) {
        float width = 0;
        for (uint32_t code : utf8Decode(text))
            width += font.widthOf(code) + glyphGap;
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
    if (ttfTextReady()) {
        ttfDraw(text, float(x), float(y), float(size), float(glyphGap), ColorTint(c, font.textColor));
        return;
    }
    if (font.ready) {
        float cursor = float(x), scale = size / 16.f;
        for (uint32_t ch : utf8Decode(text)) {
            const auto *s = font.glyphOf(ch);
            if (s && ch != ' ')
                DrawTexturePro(s->texture, {0, 0, float(s->texture.width), float(s->texture.height)},
                               {cursor, float(y), s->texture.width * scale, s->texture.height * scale},
                               {0, 0}, 0, c);
            cursor += (font.widthOf(ch) + glyphGap) * scale;
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
    if (ttfTextReady()) {
        label(text, int(bounds.x + (bounds.width - measure(text, size)) / 2),
              int(bounds.y + (bounds.height - size) / 2), size, color);
        return;
    }
    float top = 0, bottom = float(size);
    bool found = false;
    if (font.ready)
        for (uint32_t ch : utf8Decode(text)) {
            const auto *glyph = font.glyphOf(ch);
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
