#include "lighting_view.hpp"
#include "primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>

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
    float light = max(playerLight, flameLight);
    finalColor = vec4(0.0, 0.0, 0.0, (1.0 - light) * (1.0 - ambient));
}
)";
void uniform(Shader shader, const char *name, const float *value, ShaderUniformDataType type) {
    SetShaderValue(shader, GetShaderLocation(shader, name), value, type);
}
} // namespace

LightingView::LightingView() : pixels_(maskSide * maskSide, BLACK) {
    Image image = GenImageColor(maskSide, maskSide, BLACK);
    visibility_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (visibility_.id)
        SetTextureFilter(visibility_, TEXTURE_FILTER_BILINEAR);
    shader_ = LoadShaderFromMemory(nullptr, fragmentShader);
}
LightingView::~LightingView() {
    if (shader_.id)
        UnloadShader(shader_);
    if (visibility_.id)
        UnloadTexture(visibility_);
}
void LightingView::update(const Grid &grid, const LevelRecord &level, RegionId region, Vec player, int radius) {
    const int visibleRadius = level.isInside ? radius : std::max(radius, 26);
    int px = int(std::floor(player.x)), py = int(std::floor(player.y));
    if (region == cachedRegion_ && px == cachedX_ && py == cachedY_ && visibleRadius == cachedRadius_)
        return;
    cachedRegion_ = region;
    cachedX_ = px;
    cachedY_ = py;
    cachedRadius_ = visibleRadius;
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
                        const std::vector<WorldObject> &objects) const {
    if (!shader_.id || !visibility_.id)
        return;
    const float screenHeight = float(H);
    const float position[2]{playerScreen.x, playerScreen.y};
    const float world[2]{player.x, player.y};
    const float origin[2]{float(originX_), float(originY_)};
    const float effectiveRadius = level.isInside ? float(std::clamp(radius, 1, 18))
                                                 : float(std::max(radius, 26));
    const float ambient = !level.isInside ? .53f : level.losDraw ? .19f : .27f;
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
}
} // namespace d2x
