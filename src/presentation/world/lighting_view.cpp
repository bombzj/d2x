#include "lighting_view.hpp"
#include <algorithm>
#include <cmath>

namespace d2x {
namespace {
// D2MOO D2Common/D2Environment.cpp (MIT; docs/licenses/D2MOO.txt).
// These are engine constants, not replacements for the mounted Levels table.
constexpr int timeRate = 128;
struct Cycle { int begin; Color color; };
constexpr std::array<Cycle, 6> normalCycles{{
    {320, {125, 144, 243, 255}}, {340, {208, 184, 131, 255}},
    {0, {255, 255, 255, 255}}, {160, {255, 255, 255, 255}},
    {180, {194, 152, 193, 255}}, {200, {125, 144, 243, 255}}
}};
constexpr std::array<Cycle, 6> actFourCycles{{
    {340, {243, 70, 243, 255}}, {350, {208, 184, 131, 255}},
    {0, {255, 20, 20, 255}}, {180, {255, 255, 30, 255}},
    {190, {20, 152, 193, 255}}, {200, {125, 144, 243, 255}}
}};
constexpr std::array<Cycle, 6> eclipseCycles{{
    {300, {0, 30, 243, 255}}, {0, {0, 30, 244, 255}},
    {60, {0, 30, 243, 255}}, {120, {0, 30, 243, 255}},
    {180, {0, 30, 244, 255}}, {240, {0, 30, 243, 255}}
}};
// Preserve the existing distance adapter until D2Client's light-generation
// table is recovered. Do not describe this spatial rule as native falloff.
float unverifiedFalloff(float distance, float radius) {
    const float t = std::clamp((distance / radius - .22f) / .78f, 0.f, 1.f);
    return 1.f - t * t * (3.f - 2.f * t);
}
Color environmentColor(int intensity, Color color) {
    return {uint8_t(intensity * color.r / 255), uint8_t(intensity * color.g / 255),
            uint8_t(intensity * color.b / 255), 255};
}
} // namespace
LightingView::LightingView() : pixels_(maskSide * maskSide, BLACK), lightPixels_(pixels_.size(), BLACK),
                               cornerPixels_(pixels_.size(), BLACK) {
    Image image = GenImageColor(maskSide, maskSide, BLACK);
    lightMap_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (lightMap_.id) SetTextureFilter(lightMap_, TEXTURE_FILTER_BILINEAR);
}
LightingView::~LightingView() {
    if (lightMap_.id) UnloadTexture(lightMap_);
}
void LightingView::resetEnvironment() {
    environments_ = {};
    frameRemainder_ = 0;
    eclipse_ = false;
}
void LightingView::setEclipse(bool active) {
    if (eclipse_ == active) return;
    eclipse_ = active;
    auto &environment = environments_[1];
    environment.cycle = active ? 0 : 2;
    environment.ticks = active ? 300 * 4 : 0;
    environment.color = active ? eclipseCycles[0].color : WHITE;
}
void LightingView::advance(float dt, const LevelRecord &level) {
    if (dt <= 0 || level.act < 0 || level.act >= int(environments_.size())) return;
    frameRemainder_ += dt * 25.f;
    const int frames = int(frameRemainder_ + .000001f);
    frameRemainder_ = std::max(0.f, frameRemainder_ - frames);
    auto &environment = environments_[size_t(level.act)];
    const bool eclipse = level.act == 1 && eclipse_;
    const int rate = eclipse ? 4 : timeRate;
    const auto &cycles = eclipse ? eclipseCycles : level.act == 3 ? actFourCycles : normalCycles;
    for (int frame = 0; frame < frames; ++frame) {
        // GAME_UpdateProgress -> GAME_UpdateEnvironment: one update per 25 Hz frame.
        ++environment.ticks;
        if (level.act == 3) environment.ticks += 15;
        else if (environment.cycle == 5) environment.ticks += level.act == 2 ? 10 : 1;
        if (environment.ticks >= 360 * rate) environment.ticks = 0;
        const int next = (environment.cycle + 1) % int(cycles.size());
        if (environment.ticks > rate * cycles[size_t(next)].begin) {
            environment.cycle = next;
            // ENVIRONMENT_UpdateTicks uses the normal table here even in Act IV.
            environment.ticks = rate * (eclipse ? eclipseCycles : normalCycles)[size_t(next)].begin;
        }
        if (level.act == 3) {
            const int target = level.id == 103 ? 128 : level.id == 104 ? 64
                             : level.id == 105 ? 56 : level.id == 106 ? 48 : 16;
            environment.intensity += environment.intensity < target ? 1
                                   : environment.intensity > target ? -1 : 0;
            if (!environment.intensity) environment.intensity = target;
        } else if (eclipse) {
            environment.intensity = std::max(32, environment.intensity - 8);
        } else if (level.id == 120) {
            environment.intensity = 200; // Rocky Summit engine override.
        } else {
            const double angle = double(environment.ticks) / timeRate * 3.14159265358979323846 / 180.0;
            float sine = float(std::sin(angle));
            if (environment.ticks >= 180 * timeRate) sine *= .5f;
            environment.intensity = std::clamp(int(double(sine) * 128.0 + 128.0 + .5),
                                               0, level.act == 4 ? 170 : 255);
        }
        const auto &current = cycles[size_t(environment.cycle)];
        const auto &following = cycles[size_t((environment.cycle + 1) % int(cycles.size()))];
        const double ratio = double(environment.ticks - rate * current.begin) /
                     double(rate * (following.begin - current.begin));
        auto lerp = [&](uint8_t from, uint8_t to) {
            return uint8_t(int(double(from) + (double(to) - from) * ratio + .5));
        };
        environment.color = {lerp(current.color.r, following.color.r), lerp(current.color.g, following.color.g),
                             lerp(current.color.b, following.color.b), 255};
        if (level.id == 120) environment.color = {245, 240, 255, 255};
    }
}
void LightingView::update(const Grid &grid, const LevelRecord &level, RegionId region, Vec player, int radius) {
    radius = std::clamp(radius, 1, 18);
    const int px = int(std::floor(player.x)), py = int(std::floor(player.y));
    if (region == cachedRegion_ && px == cachedX_ && py == cachedY_ && radius == cachedRadius_ &&
        grid.obstacleRevision == cachedObstacleRevision_) return;
    cachedRegion_ = region;
    cachedX_ = px;
    cachedY_ = py;
    cachedRadius_ = radius;
    cachedObstacleRevision_ = grid.obstacleRevision;
    originX_ = px - maskRadius;
    originY_ = py - maskRadius;
    for (int y = 0; y < maskSide; ++y)
        for (int x = 0; x < maskSide; ++x) {
            const Vec target{originX_ + x + .5f, originY_ + y + .5f};
            const bool visible = (target - player).length() <= radius + 1.f &&
                                 (!level.isInside || grid.lightSegment(player, target));
            pixels_[size_t(y) * maskSide + x] = visible ? WHITE : BLACK;
        }
}
void LightingView::draw(const PaletteBlendView &palette, const LevelRecord &level, Vec player, Vec playerScreen,
                        float zoom, int radius, std::span<const SceneLight> lights) const {
    if (!lightMap_.id) return;
    const auto &environment = environments_[size_t(std::clamp(level.act, 0, 4))];
    const Color ambient = level.isInside
        ? environmentColor(std::clamp(level.lightIntensity, 0, 255),
                           {uint8_t(level.lightRed), uint8_t(level.lightGreen), uint8_t(level.lightBlue), 255})
        : environmentColor(environment.intensity, environment.color);
    std::fill(lightPixels_.begin(), lightPixels_.end(), ambient);
    auto add = [&](const SceneLight &source, bool playerSource) {
        if (source.radius <= 0) return;
        const int left = std::max(0, int(std::floor(source.position.x - source.radius)) - originX_);
        const int top = std::max(0, int(std::floor(source.position.y - source.radius)) - originY_);
        const int right = std::min(maskSide, int(std::ceil(source.position.x + source.radius)) - originX_ + 1);
        const int bottom = std::min(maskSide, int(std::ceil(source.position.y + source.radius)) - originY_ + 1);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const size_t index = size_t(y) * maskSide + x;
                if (playerSource && !pixels_[index].r) continue;
                const float distance = (Vec{originX_ + x + .5f, originY_ + y + .5f} - source.position).length();
                const float intensity = unverifiedFalloff(distance, source.radius);
                auto &pixel = lightPixels_[index];
                pixel.r = std::max(pixel.r, uint8_t(source.color.r * intensity));
                pixel.g = std::max(pixel.g, uint8_t(source.color.g * intensity));
                pixel.b = std::max(pixel.b, uint8_t(source.color.b * intensity));
            }
    };
    add({player, float(std::clamp(radius, 1, 18)), WHITE}, true);
    for (const auto &source : lights) add(source, false);
    // D2Gfx CmnSubtile.cpp's high-quality tile path takes the integer mean
    // of four neighbouring light samples at each subtile corner before
    // Gouraud interpolation. Keep that neighbourhood averaging in this
    // view adapter too; raw radial samples expose sharp contour boundaries.
    const auto sample = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < maskSide && y < maskSide
            ? lightPixels_[size_t(y) * maskSide + x] : ambient;
    };
    for (int y = 0; y < maskSide; ++y)
        for (int x = 0; x < maskSide; ++x) {
            const auto a = sample(x - 1, y - 1), b = sample(x, y - 1);
            const auto c = sample(x - 1, y), d = sample(x, y);
            cornerPixels_[size_t(y) * maskSide + x] = {
                uint8_t((int(a.r) + b.r + c.r + d.r) >> 2),
                uint8_t((int(a.g) + b.g + c.g + d.g) >> 2),
                uint8_t((int(a.b) + b.b + c.b + d.b) >> 2), 255};
        }
    UpdateTexture(lightMap_, cornerPixels_.data());
    palette.drawLighting(lightMap_, player, playerScreen, {float(originX_), float(originY_)}, zoom, ambient);
}
} // namespace d2x
