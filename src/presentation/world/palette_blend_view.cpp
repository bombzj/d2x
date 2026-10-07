#include "palette_blend_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>
#include <array>
#include <rlgl.h>
#include <stdexcept>

namespace d2x {
namespace {
constexpr const char *paletteLookup = R"(
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D destination;
uniform sampler2D palette;
uniform sampler2D paletteIndices;
int paletteIndex(vec3 background) {
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
    return backgroundIndex;
}
)";
constexpr const char *fragment = R"(
uniform sampler2D screenTable;
void main() {
    vec4 source = texture(texture0, fragTexCoord);
    if (source.a < 0.5) discard;
    int backgroundIndex = paletteIndex(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb);
    int sourceIndex = int(round(source.r * 255.0));
    finalColor = texelFetch(screenTable, ivec2(backgroundIndex, sourceIndex), 0);
}
)";
constexpr const char *lightingFragment = R"(
uniform sampler2D lightTable;
uniform vec2 playerScreen;
uniform vec2 playerWorld;
uniform vec2 maskOrigin;
uniform float screenHeight;
uniform float zoom;
uniform vec3 ambient;
void main() {
    vec2 pixel = vec2(gl_FragCoord.x, screenHeight - gl_FragCoord.y);
    vec2 delta = (pixel - playerScreen) / zoom;
    vec2 world = playerWorld + vec2(delta.x / 32.0 + delta.y / 16.0,
                                    delta.y / 16.0 - delta.x / 32.0);
    vec2 position = world - maskOrigin;
    vec3 light = ambient;
    if (all(greaterThanEqual(position, vec2(0.0))) &&
        all(lessThan(position, vec2(textureSize(texture0, 0)))))
        // Light texels are subtile corners, not centers: keep the integer
        // world corner aligned with its texel center for Gouraud sampling.
        light = max(light, texture(texture0, (position + 0.5) / vec2(textureSize(texture0, 0))).rgb);
    vec3 background = texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb;
    // D2Gfx CmnSubtile: uint8 intensity >> 3 selects PL2 Shadows[0..31].
    // The software scalar table does not establish hardware colored-light
    // rules. Keep the pre-existing RGB multiplication for colored samples;
    // do not invent per-channel PL2 transforms and call them native.
    if (light.r != light.g || light.r != light.b) {
        finalColor = vec4(background * light, 1.0);
        return;
    }
    int index = paletteIndex(background);
    int row = clamp(int(light.r * 255.0) / 8, 0, 31);
    finalColor = texelFetch(lightTable, ivec2(index, row), 0);
}
)";
constexpr const char *rectangleFragment = R"(
uniform sampler2D rectangleTable;
uniform int rectangleColor;
void main() {
    int backgroundIndex = paletteIndex(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb);
    finalColor = texelFetch(rectangleTable, ivec2(rectangleColor, backgroundIndex), 0);
}
)";
Texture2D upload(const Color *pixels, int width, int height) {
    Image image{const_cast<Color *>(pixels), width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}
} // namespace
PaletteBlendView::PaletteBlendView(Archives &archives, int act) {
    // OpenD2 PL2File::pScreen and OpenDiablo2 PL2::AdditiveBlend.
    // 256 RGBA entries, 49 one-index transforms, then three alpha tables.
    constexpr size_t screenOffset = 0x33500;
    const auto bytes = archives.read("data/global/palette/act" + std::to_string(act + 1) + "/pal.pl2");
    if (bytes.size() < screenOffset + 256 * 256)
        throw std::runtime_error("Original Act 1 PL2 screen table is truncated");
    std::array<Color, 256> colors{};
    for (size_t i = 0; i < colors.size(); ++i)
        colors[i] = {bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], 255};
    colors_ = colors;
    std::vector<Color> blend(256 * 256);
    for (size_t i = 0; i < blend.size(); ++i)
        blend[i] = colors[bytes[screenOffset + i]];
    palette_ = upload(colors.data(), 256, 1);
    screenTable_ = upload(blend.data(), 256, 256);
    // D2DDraw 1.13c RVA 0x6A25 selects trans[2] for DrawBox mode 0;
    // RVA 0x6850 indexes destination * 256 + the solid color index.
    for (size_t i = 0; i < blend.size(); ++i)
        blend[i] = colors[bytes[0x23500 + i]];
    rectangleTable_ = upload(blend.data(), 256, 256);
    std::vector<Color> light(32 * 256);
    for (size_t i = 0; i < light.size(); ++i)
        light[i] = colors[bytes[0x400 + i]];
    lightTable_ = upload(light.data(), 256, 32);
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
    const auto blendSource = std::string(paletteLookup) + fragment;
    const auto lightSource = std::string(paletteLookup) + lightingFragment;
    const auto rectangleSource = std::string(paletteLookup) + rectangleFragment;
    shader_ = LoadShaderFromMemory(nullptr, blendSource.c_str());
    lightingShader_ = LoadShaderFromMemory(nullptr, lightSource.c_str());
    rectangleShader_ = LoadShaderFromMemory(nullptr, rectangleSource.c_str());
    destinationLocation_ = GetShaderLocation(shader_, "destination");
    paletteLocation_ = GetShaderLocation(shader_, "palette");
    tableLocation_ = GetShaderLocation(shader_, "screenTable");
    indicesLocation_ = GetShaderLocation(shader_, "paletteIndices");
    if (!palette_.id || !screenTable_.id || !rectangleTable_.id || !lightTable_.id || !paletteIndices_.id || !destination_.id ||
        destinationLocation_ < 0 || paletteLocation_ < 0 || tableLocation_ < 0 || indicesLocation_ < 0 ||
        GetShaderLocation(lightingShader_, "lightTable") < 0 ||
        GetShaderLocation(rectangleShader_, "rectangleTable") < 0 ||
        GetShaderLocation(rectangleShader_, "rectangleColor") < 0) {
        UnloadShader(rectangleShader_);
        UnloadShader(lightingShader_);
        UnloadShader(shader_);
        UnloadRenderTexture(destination_);
        UnloadTexture(paletteIndices_);
        UnloadTexture(screenTable_);
        UnloadTexture(rectangleTable_);
        UnloadTexture(lightTable_);
        UnloadTexture(palette_);
        throw std::runtime_error("Original PL2 blend resources or shader are unavailable");
    }
}
PaletteBlendView::~PaletteBlendView() {
    UnloadShader(rectangleShader_);
    UnloadShader(lightingShader_);
    UnloadShader(shader_);
    UnloadRenderTexture(destination_);
    UnloadTexture(screenTable_);
    UnloadTexture(rectangleTable_);
    UnloadTexture(lightTable_);
    UnloadTexture(paletteIndices_);
    UnloadTexture(palette_);
}
void PaletteBlendView::drawRectangle(Rectangle bounds, Color color) const {
    const int left = std::max(0, int(bounds.x)), top = std::max(0, int(bounds.y));
    const int right = std::min(W, int(bounds.x + bounds.width));
    const int bottom = std::min(H, int(bounds.y + bounds.height));
    if (left >= right || top >= bottom) return;
    // D2CMP 1.13c RVA 0x9D30: squared RGB distance, first index on a tie.
    int index = 0, nearest = 3 * 255 * 255 + 1;
    for (int i = 0; i < 256; ++i) {
        const auto c = colors_[size_t(i)];
        const int r = int(c.r) - color.r, g = int(c.g) - color.g, b = int(c.b) - color.b;
        const int distance = r * r + g * g + b * b;
        if (distance < nearest) { nearest = distance; index = i; }
    }
    rlDrawRenderBatchActive();
    const auto target = rlGetActiveFramebuffer();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, target);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, destination_.id);
    rlBlitFramebuffer(left, H - bottom, right, H - top,
                      left, H - bottom, right, H - top, 0x00004000);
    rlEnableFramebuffer(target);
    BeginShaderMode(rectangleShader_);
    auto texture = [&](const char *name, Texture2D value) {
        SetShaderValueTexture(rectangleShader_, GetShaderLocation(rectangleShader_, name), value);
    };
    texture("destination", destination_.texture);
    texture("palette", palette_);
    texture("paletteIndices", paletteIndices_);
    texture("rectangleTable", rectangleTable_);
    SetShaderValue(rectangleShader_, GetShaderLocation(rectangleShader_, "rectangleColor"), &index, SHADER_UNIFORM_INT);
    DrawRectangle(left, top, right - left, bottom - top, WHITE);
    EndShaderMode();
}
void PaletteBlendView::drawLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom,
                                    Color ambient) const {
    rlDrawRenderBatchActive();
    const auto target = rlGetActiveFramebuffer();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, target);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, destination_.id);
    rlBlitFramebuffer(0, HUD, W, H, 0, HUD, W, H, 0x00004000);
    rlEnableFramebuffer(target);
    BeginShaderMode(lightingShader_);
    auto set = [&](const char *name, const void *value, ShaderUniformDataType type) {
        SetShaderValue(lightingShader_, GetShaderLocation(lightingShader_, name), value, type);
    };
    const float height = float(H);
    set("screenHeight", &height, SHADER_UNIFORM_FLOAT);
    set("playerScreen", &playerScreen, SHADER_UNIFORM_VEC2);
    set("playerWorld", &player, SHADER_UNIFORM_VEC2);
    set("maskOrigin", &origin, SHADER_UNIFORM_VEC2);
    set("zoom", &zoom, SHADER_UNIFORM_FLOAT);
    const float environment[3]{ambient.r / 255.f, ambient.g / 255.f, ambient.b / 255.f};
    set("ambient", environment, SHADER_UNIFORM_VEC3);
    SetShaderValueTexture(lightingShader_, GetShaderLocation(lightingShader_, "destination"), destination_.texture);
    SetShaderValueTexture(lightingShader_, GetShaderLocation(lightingShader_, "palette"), palette_);
    SetShaderValueTexture(lightingShader_, GetShaderLocation(lightingShader_, "paletteIndices"), paletteIndices_);
    SetShaderValueTexture(lightingShader_, GetShaderLocation(lightingShader_, "lightTable"), lightTable_);
    DrawTexturePro(lightMap, {0, 0, float(lightMap.width), float(lightMap.height)},
                   {0, 0, float(W), float(H - HUD)}, {}, 0, WHITE);
    EndShaderMode();
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
