#include "lighting_view.hpp"
#include "primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <rlgl.h>

namespace d2x {
namespace {
constexpr int maxFlames = 16;
constexpr const char *fragmentShader = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform float screenHeight;
uniform vec2 playerScreen;
uniform vec2 playerWorld;
uniform vec2 maskOrigin;
uniform float zoom;
uniform float radius;
uniform float ambient;
uniform sampler2D visibility;
uniform sampler2D missileLighting;
uniform int flameCount;
uniform vec2 flameWorld[16];
void main() {
    vec2 pixel = vec2(gl_FragCoord.x, screenHeight - gl_FragCoord.y);
    vec2 delta = (pixel - playerScreen) / zoom;
    vec2 world = playerWorld + vec2(delta.x / 32.0 + delta.y / 16.0,
                                      delta.y / 16.0 - delta.x / 32.0);
    vec2 maskPosition = world - maskOrigin;
    float seen = 0.0;
    if (all(greaterThanEqual(maskPosition, vec2(0.0))) &&
        all(lessThan(maskPosition, vec2(textureSize(visibility, 0)))))
        seen = texture(visibility, maskPosition / vec2(textureSize(visibility, 0))).r;
    float distanceToPlayer = length(world - playerWorld);
    float radialLight = 1.0 - smoothstep(radius * 0.22, radius, distanceToPlayer);
    // A circular fill remains visible behind blockers; only the brighter part casts shadows.
    float playerLight = max(radialLight * 0.35, radialLight * seen);
    float flameLight = 0.0;
    for (int i = 0; i < flameCount; ++i) {
        float distanceToFlame = length(world - flameWorld[i]);
        flameLight = max(flameLight, (1.0 - smoothstep(1.0, 7.0, distanceToFlame)) * 0.72);
    }
    vec3 missileLight = vec3(0.0);
    if (all(greaterThanEqual(maskPosition, vec2(0.0))) &&
        all(lessThan(maskPosition, vec2(textureSize(missileLighting, 0)))))
        missileLight = texture(missileLighting, maskPosition / vec2(textureSize(missileLighting, 0))).rgb;
    vec3 light = max(vec3(max(playerLight, flameLight)), missileLight);
    finalColor = vec4(vec3(ambient) + light * (1.0 - ambient), 1.0);
}
)";
void uniform(Shader shader, const char *name, const float *value, ShaderUniformDataType type) {
    SetShaderValue(shader, GetShaderLocation(shader, name), value, type);
}
} // namespace

LightingView::LightingView() : pixels_(maskSide * maskSide, BLACK), missilePixels_(pixels_.size(), BLACK) {
    Image image = GenImageColor(maskSide, maskSide, BLACK);
    visibility_ = LoadTextureFromImage(image);
    missileLighting_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (visibility_.id)
        SetTextureFilter(visibility_, TEXTURE_FILTER_BILINEAR);
    if (missileLighting_.id)
        SetTextureFilter(missileLighting_, TEXTURE_FILTER_BILINEAR);
    shader_ = LoadShaderFromMemory(nullptr, fragmentShader);
}
LightingView::~LightingView() {
    if (shader_.id)
        UnloadShader(shader_);
    if (visibility_.id)
        UnloadTexture(visibility_);
    if (missileLighting_.id)
        UnloadTexture(missileLighting_);
}
void LightingView::update(const Grid &grid, const LevelRecord &level, RegionId region, Vec player, int radius) {
    const int visibleRadius = level.isInside ? radius : std::max(radius, 26);
    int px = int(std::floor(player.x)), py = int(std::floor(player.y));
    if (region == cachedRegion_ && px == cachedX_ && py == cachedY_ && visibleRadius == cachedRadius_ &&
        grid.obstacleRevision == cachedObstacleRevision_)
        return;
    cachedRegion_ = region;
    cachedX_ = px;
    cachedY_ = py;
    cachedRadius_ = visibleRadius;
    cachedObstacleRevision_ = grid.obstacleRevision;
    originX_ = px - maskRadius;
    originY_ = py - maskRadius;
    for (int y = 0; y < maskSide; ++y)
        for (int x = 0; x < maskSide; ++x) {
            const int wx = originX_ + x, wy = originY_ + y;
            const Vec target{wx + .5f, wy + .5f};
            const bool inRange = (target - player).length() <= visibleRadius + 1.f;
            // Outdoor player light has no terrain-shaped shadow; dungeons retain
            // the DT1 blocker mask for their brighter direct contribution.
            const bool visible = inRange && (!level.isInside || grid.lightSegment(player, target));
            pixels_[size_t(y) * maskSide + x] = visible ? WHITE : BLACK;
        }
    if (visibility_.id)
        UpdateTexture(visibility_, pixels_.data());
}
void LightingView::draw(const LevelRecord &level, Vec player, Vec playerScreen, float zoom, int radius,
                        const std::vector<WorldObject> &objects, std::span<const MissileLight> missiles) const {
    if (!shader_.id || !visibility_.id || !missileLighting_.id)
        return;
    const float screenHeight = float(H);
    const float position[2]{playerScreen.x, playerScreen.y};
    const float world[2]{player.x, player.y};
    const float origin[2]{float(originX_), float(originY_)};
    const float effectiveRadius = level.isInside ? float(std::clamp(radius, 1, 18))
                                                 : float(std::max(radius, 26));
    const float ambient = !level.isInside ? .53f : level.losDraw ? .19f : .27f;
    std::fill(missilePixels_.begin(), missilePixels_.end(), BLACK);
    for (const auto &source : missiles) {
        if (source.radius <= 0) continue;
        const int left = std::max(0, int(std::floor(source.position.x - source.radius)) - originX_);
        const int top = std::max(0, int(std::floor(source.position.y - source.radius)) - originY_);
        const int right = std::min(maskSide, int(std::ceil(source.position.x + source.radius)) - originX_);
        const int bottom = std::min(maskSide, int(std::ceil(source.position.y + source.radius)) - originY_);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const float distance = (Vec{originX_ + x + .5f, originY_ + y + .5f} - source.position).length();
                // Reuse this view's radius curve. The original D2Client falloff
                // and colored-light occlusion remain unverified; no new rule.
                const float t = std::clamp((distance / source.radius - .22f) / .78f, 0.f, 1.f);
                const float intensity = 1.f - t * t * (3.f - 2.f * t);
                auto &pixel = missilePixels_[size_t(y) * maskSide + x];
                pixel.r = std::max(pixel.r, uint8_t(source.color.r * intensity));
                pixel.g = std::max(pixel.g, uint8_t(source.color.g * intensity));
                pixel.b = std::max(pixel.b, uint8_t(source.color.b * intensity));
            }
    }
    UpdateTexture(missileLighting_, missilePixels_.data());
    // Multiply scene channels by their illumination, preserving blue light.
    rlSetBlendFactors(0, 0x0300, 0x8006); // ZERO, SRC_COLOR, FUNC_ADD
    BeginBlendMode(BLEND_CUSTOM);
    // Changing shader flushes raylib's previous batch and clears registered sampler textures.
    BeginShaderMode(shader_);
    uniform(shader_, "screenHeight", &screenHeight, SHADER_UNIFORM_FLOAT);
    uniform(shader_, "playerScreen", position, SHADER_UNIFORM_VEC2);
    uniform(shader_, "playerWorld", world, SHADER_UNIFORM_VEC2);
    uniform(shader_, "maskOrigin", origin, SHADER_UNIFORM_VEC2);
    uniform(shader_, "zoom", &zoom, SHADER_UNIFORM_FLOAT);
    uniform(shader_, "radius", &effectiveRadius, SHADER_UNIFORM_FLOAT);
    uniform(shader_, "ambient", &ambient, SHADER_UNIFORM_FLOAT);
    SetShaderValueTexture(shader_, GetShaderLocation(shader_, "visibility"), visibility_);
    SetShaderValueTexture(shader_, GetShaderLocation(shader_, "missileLighting"), missileLighting_);
    std::array<float, maxFlames * 2> flames{};
    int count = 0;
    for (const auto &object : objects) {
        if (!object.flame || object.questHidden || (object.pos - player).length() > 28.f)
            continue;
        flames[size_t(count) * 2] = object.pos.x;
        flames[size_t(count) * 2 + 1] = object.pos.y;
        if (++count == maxFlames)
            break;
    }
    SetShaderValue(shader_, GetShaderLocation(shader_, "flameCount"), &count, SHADER_UNIFORM_INT);
    if (count)
        SetShaderValueV(shader_, GetShaderLocation(shader_, "flameWorld"), flames.data(), SHADER_UNIFORM_VEC2,
                        count);
    DrawRectangle(0, 0, W, H - HUD, WHITE);
    EndShaderMode();
    EndBlendMode();
}
} // namespace d2x
