#include "palette_blend_view.hpp"
#include "presentation/graphics/primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <rlgl.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace d2x {
namespace {
enum LightUniform {
    LightScreenHeight, LightPlayerScreen, LightPlayerWorld, LightOrigin, LightZoom, LightAmbient,
    LightSampleCount, LightHighlight, LightSamples, LightIndexed, LightMap, LightPalette, LightIndices, LightTable
};
enum LitBlendUniform { BlendLight, BlendDestination, BlendPalette, BlendLightTable, BlendScreenTable };
enum WallBlendUniform { WallSampleCount, WallSamples, WallDestination, WallLightTable, WallIndices, WallTransTable };
constexpr const char *wallLighting = R"(
uniform int sampleCount;
uniform vec4 lightSamples[6];
int wallLightRow(vec2 uv, ivec2 size, bool translucent) {
    int left = int(round(lightSamples[0].a * 255.0));
    if (sampleCount == 1) return left / 8;
    int x = int(floor(uv.x * float(size.x)));
    int column = clamp(x / 32, 0, 4);
    left = int(round(lightSamples[column].a * 255.0));
    int right = int(round(lightSamples[column + 1].a * 255.0));
    // CmnSubtile: transparent tiles always use the Gouraud cache; opaque
    // tiles enter it only at delta >= 10. Cache endpoints are already 5-bit.
    if (!translucent && abs(right - left) < 10) return left / 8;
    return (32 * (left / 8) + (right / 8 - left / 8) * (x % 32)) / 32;
}
)";
constexpr const char *paletteLookup = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
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
uniform int nativeOpaque;
void main() {
    vec4 source = texture(texture0, fragTexCoord);
    if (source.a < 0.5) discard;
    int sourceIndex = int(round(source.r * 255.0));
    if (nativeOpaque != 0) {
        finalColor = texelFetch(palette, ivec2(sourceIndex, 0), 0);
        return;
    }
    int backgroundIndex = paletteIndex(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb);
    finalColor = texelFetch(screenTable, ivec2(backgroundIndex, sourceIndex), 0);
}
)";
constexpr const char *lightingFragment = R"(
uniform sampler2D lightTable;
uniform sampler2D lightMap;
uniform vec2 playerScreen;
uniform vec2 playerWorld;
uniform vec2 maskOrigin;
uniform float screenHeight;
uniform float zoom;
// Packed native light: normalized color in RGB, independent intensity in A.
uniform vec4 ambient;
uniform int sourceIndexed;
uniform vec2 highlightTransform;
void main() {
    vec4 source = texture(texture0, fragTexCoord);
    if (source.a == 0.0) discard;
    int originalIndex = int(round(source.r * 255.0));
    if (sourceIndexed != 0) source.rgb = texelFetch(palette, ivec2(originalIndex, 0), 0).rgb;
    source *= fragColor;
    source.rgb = clamp((source.rgb * highlightTransform.x - 0.5) * highlightTransform.y + 0.5, 0.0, 1.0);
    vec2 pixel = vec2(gl_FragCoord.x, screenHeight - gl_FragCoord.y);
    vec2 delta = (pixel - playerScreen) / zoom;
    vec2 world = playerWorld + vec2(delta.x / 32.0 + delta.y / 16.0,
                                    delta.y / 16.0 - delta.x / 32.0);
    vec2 position = world - maskOrigin;
    vec4 light = ambient;
    if (all(greaterThanEqual(position, vec2(0.0))) &&
        all(lessThan(position, vec2(textureSize(lightMap, 0)))))
        // Light texels are subtile corners, not centers: keep the integer
        // world corner aligned with its texel center for Gouraud sampling.
        light = texture(lightMap, (position + 0.5) / vec2(textureSize(lightMap, 0)));
    if (sampleCount == 1) light = lightSamples[0];
    else if (sampleCount == 6) {
        // D2Gdi 5F0C / D2DDraw 73DC: block x >> 5 selects two of the
        // six wall samples. They are duplicated vertically, even on tall walls.
        light = vec4(1.0,1.0,1.0,float(wallLightRow(fragTexCoord,textureSize(texture0,0),false) * 8) / 255.0);
    }
    // GDI/DDraw consume the independent intensity for floors and cels too.
    // Switching to RGB multiplication for a colored source would discard
    // PL2 and darken an otherwise saturated ice-armor light.
    int index = sourceIndexed != 0 && all(equal(fragColor.rgb, vec3(1.0))) &&
                all(equal(highlightTransform, vec2(1.0))) ? originalIndex : paletteIndex(source.rgb);
    int intensity = sampleCount == 0 ? int(light.a * 255.0) : int(round(light.a * 255.0));
    int row = clamp(intensity / 8, 0, 31);
    finalColor = vec4(texelFetch(lightTable, ivec2(index, row), 0).rgb, source.a);
}
)";
constexpr const char *wallBlendFragment = R"(
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D destination;
uniform sampler2D lightTable;
uniform sampler2D paletteIndices;
uniform sampler2D transparentTable;
vec3 paletteColor(int index) { return texelFetch(lightTable,ivec2(index,32),0).rgb; }
int backgroundIndex(vec3 color) {
    ivec3 rgb = ivec3(round(color * 255.0)) / 4;
    int key = rgb.r + rgb.g * 64 + rgb.b * 4096;
    int result = int(round(texelFetch(paletteIndices,ivec2(key % 512,key / 512),0).r * 255.0));
    if (all(equal(paletteColor(result),color))) return result;
    float nearest = 4.0;
    for (int i = 0; i < 256; ++i) {
        vec3 delta = paletteColor(i) - color;
        float distance = dot(delta,delta);
        if (distance < nearest) { nearest = distance; result = i; }
        if (distance == 0.0) break;
    }
    return result;
}
)";
constexpr const char *wallBlendMain = R"(
void main() {
    vec4 source = texture(texture0,fragTexCoord);
    if (source.a < 0.5) discard;
    int sourceIndex = int(round(source.r * 255.0));
    int row = wallLightRow(fragTexCoord,textureSize(texture0,0),true);
    int litIndex = int(round(texelFetch(lightTable,ivec2(sourceIndex,row),0).a * 255.0));
    int background = backgroundIndex(texelFetch(destination,ivec2(gl_FragCoord.xy),0).rgb);
    // Roof/transparent-wall helper indexes trans[litSource][destination].
    // Existing UI textures are transposed; use the corresponding coordinates.
    finalColor = texelFetch(transparentTable,ivec2(litIndex,background),0);
}
)";
constexpr const char *litBlendFragment = R"(
#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D palette;
uniform sampler2D lightTable;
uniform sampler2D destination;
uniform sampler2D screenTable;
uniform vec4 sourceLight;
int closestIndex(vec3 color) {
    int result = 0; float nearest = 4.0;
    for (int i = 0; i < 256; ++i) {
        vec3 delta = texelFetch(palette, ivec2(i, 0), 0).rgb - color;
        float distance = dot(delta, delta);
        if (distance < nearest) { nearest = distance; result = i; }
        if (distance == 0.0) break;
    }
    return result;
}
void main() {
    vec4 source = texture(texture0, fragTexCoord);
    if (source.a < 0.5) discard;
    int index = int(round(source.r * 255.0));
    vec3 lit = texelFetch(lightTable, ivec2(index, clamp(int(round(sourceLight.a * 255.0)) / 8, 0, 31)), 0).rgb;
    int background = closestIndex(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).rgb);
    finalColor = texelFetch(screenTable, ivec2(background, closestIndex(lit)), 0);
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
    // Original TRANS50, trans[1][destination][source], for party portraits.
    for (size_t source = 0; source < 256; ++source)
        for (size_t destination = 0; destination < 256; ++destination)
            blend[source * 256 + destination] = colors[bytes[0x13500 + destination * 256 + source]];
    halfTable_ = upload(blend.data(), 256, 256);
    // Socket holes use CelDraw mode 0, trans[2], before the child icon.
    for (size_t source = 0; source < 256; ++source)
        for (size_t destination = 0; destination < 256; ++destination)
            blend[source * 256 + destination] = colors[bytes[0x23500 + destination * 256 + source]];
    socketTable_ = upload(blend.data(), 256, 256);
    // CelDraw mode 2 uses trans[0]; automap Everything uses this on full size.
    for (size_t source = 0; source < 256; ++source)
        for (size_t destination = 0; destination < 256; ++destination)
            blend[source * 256 + destination] = colors[bytes[0x3500 + destination * 256 + source]];
    quarterTable_ = upload(blend.data(), 256, 256);
    // D2WinPalette builds the HIGHLIGHT shift by brightening current PL2 RGB
    // to 170% and finding the nearest original palette index. D2DDraw RVA
    // 6DA7 selects that shift (PaletteTable + 118) for CelDraw mode 7.
    for (size_t source = 0; source < colors.size(); ++source) {
        const auto color = colors[source];
        const auto brighten = [](uint8_t component) { return uint8_t(std::min(255, 170 * int(component) / 100)); };
        const auto highlighted = paletteColor({brighten(color.r), brighten(color.g), brighten(color.b), 255});
        std::fill_n(blend.begin() + source * 256, 256, highlighted);
    }
    highlightTable_ = upload(blend.data(), 256, 256);
    // D2DDraw 1.13c RVA 0x6A25 selects trans[2] for DrawBox mode 0;
    // RVA 0x6850 indexes destination * 256 + the solid color index.
    for (size_t i = 0; i < blend.size(); ++i)
        blend[i] = colors[bytes[0x23500 + i]];
    rectangleTable_ = upload(blend.data(), 256, 256);
    std::vector<Color> light(33 * 256);
    for (size_t i = 0; i < 32 * 256; ++i) {
        light[i] = colors[bytes[0x400 + i]];
        light[i].a = bytes[0x400 + i]; // Preserve the exact index across PL2 -> trans.
    }
    std::copy(colors.begin(),colors.end(),light.begin() + 32 * 256);
    lightTable_ = upload(light.data(), 256, 33);
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
    const auto lightSource = std::string(paletteLookup) + wallLighting + lightingFragment;
    const auto wallSource = std::string(wallBlendFragment) + wallLighting + wallBlendMain;
    const auto rectangleSource = std::string(paletteLookup) + rectangleFragment;
    shader_ = LoadShaderFromMemory(nullptr, blendSource.c_str());
    lightingShader_ = LoadShaderFromMemory(nullptr, lightSource.c_str());
    litBlendShader_ = LoadShaderFromMemory(nullptr, litBlendFragment);
    wallBlendShader_ = LoadShaderFromMemory(nullptr, wallSource.c_str());
    const std::array<const char *,14> lightingNames{{"screenHeight", "playerScreen", "playerWorld", "maskOrigin",
        "zoom", "ambient", "sampleCount", "highlightTransform", "lightSamples[0]", "sourceIndexed",
        "lightMap", "palette", "paletteIndices", "lightTable"}};
    for (size_t i = 0; i < lightingNames.size(); ++i)
        lightingLocations_[i] = GetShaderLocation(lightingShader_, lightingNames[i]);
    const std::array<const char *,5> blendNames{{"sourceLight", "destination", "palette", "lightTable", "screenTable"}};
    for (size_t i = 0; i < blendNames.size(); ++i)
        litBlendLocations_[i] = GetShaderLocation(litBlendShader_, blendNames[i]);
    const std::array<const char *,6> wallNames{{"sampleCount","lightSamples[0]","destination","lightTable","paletteIndices","transparentTable"}};
    for (size_t i = 0; i < wallNames.size(); ++i)
        wallBlendLocations_[i] = GetShaderLocation(wallBlendShader_,wallNames[i]);
    rectangleShader_ = LoadShaderFromMemory(nullptr, rectangleSource.c_str());
    destinationLocation_ = GetShaderLocation(shader_, "destination");
    paletteLocation_ = GetShaderLocation(shader_, "palette");
    tableLocation_ = GetShaderLocation(shader_, "screenTable");
    indicesLocation_ = GetShaderLocation(shader_, "paletteIndices");
    opaqueLocation_ = GetShaderLocation(shader_, "nativeOpaque");
    if (!palette_.id || !screenTable_.id || !halfTable_.id || !socketTable_.id || !quarterTable_.id || !highlightTable_.id || !rectangleTable_.id || !lightTable_.id || !paletteIndices_.id || !destination_.id ||
        destinationLocation_ < 0 || paletteLocation_ < 0 || tableLocation_ < 0 || indicesLocation_ < 0 || opaqueLocation_ < 0 ||
        std::ranges::any_of(lightingLocations_, [](int location) { return location < 0; }) ||
        std::ranges::any_of(litBlendLocations_, [](int location) { return location < 0; }) ||
        std::ranges::any_of(wallBlendLocations_, [](int location) { return location < 0; }) ||
        GetShaderLocation(rectangleShader_, "rectangleTable") < 0 ||
        GetShaderLocation(rectangleShader_, "rectangleColor") < 0) {
        UnloadShader(rectangleShader_);
        UnloadShader(lightingShader_);
        UnloadShader(litBlendShader_);
        UnloadShader(wallBlendShader_);
        UnloadShader(shader_);
        UnloadRenderTexture(destination_);
        UnloadTexture(paletteIndices_);
        UnloadTexture(screenTable_);
        UnloadTexture(halfTable_);
        UnloadTexture(socketTable_);
        UnloadTexture(quarterTable_);
        UnloadTexture(highlightTable_);
        UnloadTexture(rectangleTable_);
        UnloadTexture(lightTable_);
        UnloadTexture(palette_);
        throw std::runtime_error("Original PL2 blend resources or shader are unavailable");
    }
}
PaletteBlendView::~PaletteBlendView() {
    UnloadShader(wallBlendShader_);
    UnloadShader(litBlendShader_);
    UnloadShader(rectangleShader_);
    UnloadShader(lightingShader_);
    UnloadShader(shader_);
    UnloadRenderTexture(destination_);
    UnloadTexture(screenTable_);
    UnloadTexture(halfTable_);
    UnloadTexture(socketTable_);
    UnloadTexture(quarterTable_);
    UnloadTexture(highlightTable_);
    UnloadTexture(rectangleTable_);
    UnloadTexture(lightTable_);
    UnloadTexture(paletteIndices_);
    UnloadTexture(palette_);
}
int PaletteBlendView::paletteIndex(Color color) const {
    // D2CMP 1.13c RVA 0x9D30: squared RGB distance, first index on a tie.
    int index = 0, nearest = 3 * 255 * 255 + 1;
    for (int i = 0; i < 256; ++i) {
        const auto c = colors_[size_t(i)];
        const int r = int(c.r) - color.r, g = int(c.g) - color.g, b = int(c.b) - color.b;
        const int distance = r * r + g * g + b * b;
        if (distance < nearest) { nearest = distance; index = i; }
    }
    return index;
}
void PaletteBlendView::drawRectangle(Rectangle bounds, Color color) const {
    const int left = std::max(0, int(bounds.x)), top = std::max(0, int(bounds.y));
    const int right = std::min(W, int(bounds.x + bounds.width));
    const int bottom = std::min(H, int(bounds.y + bounds.height));
    if (left >= right || top >= bottom) return;
    const int index = paletteIndex(color);
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
void PaletteBlendView::setLighting(Texture2D lightMap, Vec player, Vec playerScreen, Vec origin, float zoom,
                                  Color ambient) {
    worldLightMap_ = lightMap; lightPlayer_ = player; lightPlayerScreen_ = playerScreen;
    lightOrigin_ = origin; lightZoom_ = zoom; lightAmbient_ = ambient;
}
void PaletteBlendView::drawWall(const Sprite *image, Vec position, std::span<const Color> samples, uint8_t alpha) const {
    if (!image || !image->texture.id || alpha < 64) return;
    if (alpha == 255) { drawLit(image,position,samples); return; }
    if (!image->indexedTexture.id || (samples.size() != 1 && samples.size() != 6))
        throw std::invalid_argument("Transparent wall requires original indices and native wall samples");
    const Rectangle bounds{float(int(position.x + image->x)),float(int(position.y + image->y)),
        float(image->texture.width),float(image->texture.height)};
    const int left = std::max(0,int(bounds.x)), top = std::max(0,int(bounds.y));
    const int right = std::min(W,int(bounds.x + bounds.width)), bottom = std::min(H,int(bounds.y + bounds.height));
    if (left >= right || top >= bottom) return;
    rlDrawRenderBatchActive();
    const auto target = rlGetActiveFramebuffer();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER,target);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER,destination_.id);
    rlBlitFramebuffer(left,H-bottom,right,H-top,left,H-bottom,right,H-top,0x00004000);
    rlEnableFramebuffer(target);
    BeginShaderMode(wallBlendShader_);
    const int count = int(samples.size());
    SetShaderValue(wallBlendShader_,wallBlendLocations_[WallSampleCount],&count,SHADER_UNIFORM_INT);
    std::array<std::array<float,4>,6> packed{};
    for (size_t i = 0; i < samples.size(); ++i) packed[i][3] = samples[i].a / 255.f;
    SetShaderValueV(wallBlendShader_,wallBlendLocations_[WallSamples],packed.data(),SHADER_UNIFORM_VEC4,6);
    // D2gfx 9667/967B/9689: 192..254, 128..191, 64..127.
    const auto table = alpha >= 192 ? quarterTable_ : alpha >= 128 ? halfTable_ : socketTable_;
    for (const auto &[uniform,texture] : std::array<std::pair<WallBlendUniform,Texture2D>,4>{{
            {WallDestination,destination_.texture},{WallLightTable,lightTable_},
            {WallIndices,paletteIndices_},{WallTransTable,table}}})
        SetShaderValueTexture(wallBlendShader_,wallBlendLocations_[uniform],texture);
    DrawTexturePro(image->indexedTexture,{0,0,bounds.width,bounds.height},bounds,{},0,WHITE);
    EndShaderMode();
}
void PaletteBlendView::drawLit(const Sprite *image, Vec position, std::span<const Color> samples,
                              Color tint, Vector2 highlight, bool screenBlend) const {
    if (!image || !image->texture.id) return;
    if (!samples.empty() && samples.size() != 1 && samples.size() != 6)
        throw std::invalid_argument("World light samples must be floor, unit, or six wall columns");
    const auto packed = [](Color color) {
        return std::array<float,4>{color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
    };
    if (screenBlend) {
        if (!image->indexedTexture.id || samples.size() != 1) return;
        const Rectangle bounds{float(int(position.x + image->x)), float(int(position.y + image->y)),
                               float(image->texture.width), float(image->texture.height)};
        const int left = std::max(0, int(bounds.x)), top = std::max(0, int(bounds.y));
        const int right = std::min(W, int(bounds.x + bounds.width));
        const int bottom = std::min(H, int(bounds.y + bounds.height));
        if (left >= right || top >= bottom) return;
        rlDrawRenderBatchActive();
        const auto target = rlGetActiveFramebuffer();
        rlBindFramebuffer(RL_READ_FRAMEBUFFER, target);
        rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, destination_.id);
        rlBlitFramebuffer(left, H - bottom, right, H - top, left, H - bottom, right, H - top, 0x00004000);
        rlEnableFramebuffer(target);
        BeginShaderMode(litBlendShader_);
        const auto light = packed(samples.front());
        SetShaderValue(litBlendShader_, litBlendLocations_[BlendLight], light.data(), SHADER_UNIFORM_VEC4);
        for (const auto &[uniform, texture] : std::array<std::pair<LitBlendUniform,Texture2D>,4>{{
                {BlendDestination, destination_.texture}, {BlendPalette, palette_},
                {BlendLightTable, lightTable_}, {BlendScreenTable, screenTable_}}})
            SetShaderValueTexture(litBlendShader_, litBlendLocations_[uniform], texture);
        DrawTexturePro(image->indexedTexture, {0,0,bounds.width,bounds.height}, bounds, {}, 0, WHITE);
        EndShaderMode();
        return;
    }
    BeginShaderMode(lightingShader_);
    const auto set = [&](LightUniform uniform, const void *value, ShaderUniformDataType type) {
        SetShaderValue(lightingShader_, lightingLocations_[uniform], value, type);
    };
    const float height = float(H);
    set(LightScreenHeight, &height, SHADER_UNIFORM_FLOAT);
    set(LightPlayerScreen, &lightPlayerScreen_, SHADER_UNIFORM_VEC2);
    set(LightPlayerWorld, &lightPlayer_, SHADER_UNIFORM_VEC2);
    set(LightOrigin, &lightOrigin_, SHADER_UNIFORM_VEC2);
    set(LightZoom, &lightZoom_, SHADER_UNIFORM_FLOAT);
    const auto ambient = packed(lightAmbient_);
    set(LightAmbient, ambient.data(), SHADER_UNIFORM_VEC4);
    const int count = int(samples.size());
    set(LightSampleCount, &count, SHADER_UNIFORM_INT);
    set(LightHighlight, &highlight, SHADER_UNIFORM_VEC2);
    std::array<std::array<float,4>,6> lightSamples{};
    for (size_t i = 0; i < samples.size(); ++i) lightSamples[i] = packed(samples[i]);
    SetShaderValueV(lightingShader_, lightingLocations_[LightSamples],
                    lightSamples.data(), SHADER_UNIFORM_VEC4, 6);
    const bool indexed = image->indexedTexture.id && image->layers.empty();
    const int sourceIndexed = indexed ? 1 : 0;
    set(LightIndexed, &sourceIndexed, SHADER_UNIFORM_INT);
    const auto bind = [&] {
        // Raylib exposes four extra sampler slots: source texture0 plus these four.
        for (const auto &[uniform, texture] : std::array<std::pair<LightUniform,Texture2D>,4>{{
                {LightMap, worldLightMap_}, {LightPalette, palette_},
                {LightIndices, paletteIndices_}, {LightTable, lightTable_}}})
            SetShaderValueTexture(lightingShader_, lightingLocations_[uniform], texture);
    };
    if (image->layers.empty()) {
        bind();
        DrawTexture(indexed ? image->indexedTexture : image->texture,
                    int(position.x + image->x), int(position.y + image->y), tint);
    } else for (const auto &layer : image->layers) {
        if (layer.softAdditive) {
            rlSetBlendFactors(0x0307, 1, 0x8006);
            BeginBlendMode(BLEND_CUSTOM);
        } else BeginBlendMode(BLEND_ALPHA);
        bind();
        DrawTexture(layer.texture, int(position.x + layer.x), int(position.y + layer.y), tint);
        EndBlendMode();
    }
    EndShaderMode();
}
void PaletteBlendView::draw(const Sprite *image, Vec position) const {
    if (!image) return;
    drawSprite(image, {float(int(position.x + image->x)), float(int(position.y + image->y)),
        float(image->texture.width), float(image->texture.height)}, screenTable_);
}
void PaletteBlendView::drawHalf(const Sprite *image, Rectangle bounds) const {
    drawSprite(image, bounds, halfTable_);
}
void PaletteBlendView::drawAutomap(const Sprite *image, Vec position, int mode) const {
    if (!image || !image->indexedTexture.id) return;
    const Rectangle bounds{float(int(position.x) + image->x), float(int(position.y) + image->y),
        float(image->texture.width), float(image->texture.height)};
    if (mode == 5) {
        const int opaque = 1;
        BeginShaderMode(shader_);
        SetShaderValue(shader_, opaqueLocation_, &opaque, SHADER_UNIFORM_INT);
        SetShaderValueTexture(shader_, paletteLocation_, palette_);
        DrawTexturePro(image->indexedTexture, {0, 0, bounds.width, bounds.height}, bounds, {}, 0, WHITE);
        EndShaderMode();
        return;
    }
    drawSprite(image, bounds, mode == 0 ? socketTable_ : mode == 1 ? halfTable_ : quarterTable_);
}
void PaletteBlendView::drawSocket(const Sprite *image, Rectangle bounds) const {
    drawSprite(image, bounds, socketTable_);
}
void PaletteBlendView::drawHighlight(const Sprite *image, Rectangle bounds) const {
    drawSprite(image, bounds, highlightTable_);
}
void PaletteBlendView::drawSprite(const Sprite *image, Rectangle bounds, Texture2D table) const {
    if (!image || !image->indexedTexture.id) return;
    const int x = int(bounds.x), y = int(bounds.y);
    const int left = std::max(0, x), top = std::max(0, y);
    const int right = std::min(W, int(std::ceil(bounds.x + bounds.width)));
    const int bottom = std::min(H, int(std::ceil(bounds.y + bounds.height)));
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
    const int opaque = 0;
    SetShaderValue(shader_, opaqueLocation_, &opaque, SHADER_UNIFORM_INT);
    SetShaderValueTexture(shader_, destinationLocation_, destination_.texture);
    SetShaderValueTexture(shader_, paletteLocation_, palette_);
    SetShaderValueTexture(shader_, tableLocation_, table);
    SetShaderValueTexture(shader_, indicesLocation_, paletteIndices_);
    DrawTexturePro(image->indexedTexture, {0,0,float(image->texture.width),float(image->texture.height)}, bounds, {}, 0, WHITE);
    EndShaderMode();
}
} // namespace d2x
