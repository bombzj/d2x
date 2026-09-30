#include "palette_blend_view.hpp"
#include "primitives.hpp"
#include <algorithm>
#include <array>
#include <rlgl.h>
#include <stdexcept>

namespace d2x {
namespace {
constexpr const char *fragment = R"(
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D destination;
uniform sampler2D palette;
uniform sampler2D screenTable;
uniform sampler2D paletteIndices;
void main() {
    vec4 source = texture(texture0, fragTexCoord);
    if (source.a < 0.5) discard;
    vec3 background = texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb;
    // The existing world buffer is RGBA. Recover its palette index; palette
    // RGB is recovered exactly for untouched palette pixels; duplicate RGB
    // indices use the first entry. Previously tinted pixels are quantized.
    ivec3 rgb = ivec3(round(background * 255.0)) / 4;
    int key = rgb.r + rgb.g * 64 + rgb.b * 4096;
    int backgroundIndex = int(round(texelFetch(paletteIndices, ivec2(key % 512, key / 512), 0).r * 255.0));
    if (any(notEqual(texelFetch(palette, ivec2(backgroundIndex, 0), 0).rgb, background))) {
        backgroundIndex = 0;
        float nearest = 4.0;
        for (int i = 0; i < 256; ++i) {
            vec3 delta = texelFetch(palette, ivec2(i, 0), 0).rgb - background;
            float distance = dot(delta, delta);
            if (distance < nearest) { nearest = distance; backgroundIndex = i; }
            if (distance == 0.0) break;
        }
    }
    int sourceIndex = int(round(source.r * 255.0));
    finalColor = texelFetch(screenTable, ivec2(backgroundIndex, sourceIndex), 0);
}
)";
Texture2D upload(const Color *pixels, int width, int height) {
    Image image{const_cast<Color *>(pixels), width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}
} // namespace
PaletteBlendView::PaletteBlendView(Archives &archives) {
    // OpenD2 PL2File::pScreen and OpenDiablo2 PL2::AdditiveBlend.
    // 256 RGBA entries, 49 one-index transforms, then three alpha tables.
    constexpr size_t screenOffset = 0x33500;
    const auto bytes = archives.read("data/global/palette/act1/pal.pl2");
    if (bytes.size() < screenOffset + 256 * 256)
        throw std::runtime_error("Original Act 1 PL2 screen table is truncated");
    std::array<Color, 256> colors{};
    for (size_t i = 0; i < colors.size(); ++i)
        colors[i] = {bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], 255};
    std::vector<Color> blend(256 * 256);
    for (size_t i = 0; i < blend.size(); ++i)
        blend[i] = colors[bytes[screenOffset + i]];
    palette_ = upload(colors.data(), 256, 1);
    screenTable_ = upload(blend.data(), 256, 256);
    // Fast inverse for unchanged palette pixels, with an exact RGB comparison
    // in the shader. Quantized-key collisions always use the full lookup.
    std::vector<Color> indices(512 * 512, Color{0, 0, 0, 255});
    for (int i = 255; i >= 0; --i) {
        const auto c = colors[size_t(i)];
        indices[c.r / 4 + (c.g / 4) * 64 + (c.b / 4) * 4096].r = uint8_t(i);
    }
    paletteIndices_ = upload(indices.data(), 512, 512);
    destination_ = LoadRenderTexture(W, H);
    SetTextureFilter(destination_.texture, TEXTURE_FILTER_POINT);
    shader_ = LoadShaderFromMemory(nullptr, fragment);
    destinationLocation_ = GetShaderLocation(shader_, "destination");
    paletteLocation_ = GetShaderLocation(shader_, "palette");
    tableLocation_ = GetShaderLocation(shader_, "screenTable");
    indicesLocation_ = GetShaderLocation(shader_, "paletteIndices");
    if (!palette_.id || !screenTable_.id || !paletteIndices_.id || !destination_.id ||
        destinationLocation_ < 0 || paletteLocation_ < 0 || tableLocation_ < 0 || indicesLocation_ < 0) {
        UnloadShader(shader_);
        UnloadRenderTexture(destination_);
        UnloadTexture(paletteIndices_);
        UnloadTexture(screenTable_);
        UnloadTexture(palette_);
        throw std::runtime_error("Original PL2 blend resources or shader are unavailable");
    }
}
PaletteBlendView::~PaletteBlendView() {
    UnloadShader(shader_);
    UnloadRenderTexture(destination_);
    UnloadTexture(screenTable_);
    UnloadTexture(paletteIndices_);
    UnloadTexture(palette_);
}
void PaletteBlendView::draw(const Sprite *image, Vec position) const {
    if (!image || !image->indexedTexture.id) return;
    const int x = int(position.x + image->x), y = int(position.y + image->y);
    const int left = std::max(0, x), top = std::max(0, y);
    const int right = std::min(W, x + image->texture.width);
    const int bottom = std::min(H - HUD, y + image->texture.height);
    if (left >= right || top >= bottom) return;
    rlDrawRenderBatchActive();
    const auto target = rlGetActiveFramebuffer();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, target);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, destination_.id);
    // Identical framebuffer coordinates preserve orientation and the scissor.
    rlBlitFramebuffer(left, H - bottom, right, H - top,
                      left, H - bottom, right, H - top, 0x00004000); // GL_COLOR_BUFFER_BIT
    rlEnableFramebuffer(target);
    BeginShaderMode(shader_);
    SetShaderValueTexture(shader_, destinationLocation_, destination_.texture);
    SetShaderValueTexture(shader_, paletteLocation_, palette_);
    SetShaderValueTexture(shader_, tableLocation_, screenTable_);
    SetShaderValueTexture(shader_, indicesLocation_, paletteIndices_);
    DrawTexture(image->indexedTexture, x, y, WHITE);
    EndShaderMode();
}
} // namespace d2x
