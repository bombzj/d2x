#include "lighting_view.hpp"
#include "content/world/world_catalog.hpp"
#include "palette_blend_view.hpp"
#include "world/navigation.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <map>
#include <tuple>

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
// D2Client 1.13c A8760/A87B0, A8C20: eighth-subtile coordinates,
// including the additional half-subtile offset of a light's center.
constexpr int lightPrecision = 8;
constexpr int maximumLightRadius = 18;
int lightCoordinate(float coordinate) {
    return int(std::floor(coordinate * lightPrecision)) + lightPrecision / 2;
}
int lightDistance(int dx, int dy) {
    dx = std::abs(dx); dy = std::abs(dy);
    return (983 * std::max(dx, dy) + 407 * std::min(dx, dy)) >> 10;
}
// A8A40-A8B58. Saturate the independent intensity before looking up its
// reciprocal; RGB rounding and saturation remain separate from intensity.
void accumulateLight(Color &sample, int contribution, Color color) {
    if (contribution <= 0) return;
    const int previous = sample.a;
    const int total = std::min(255, previous + contribution);
    const unsigned reciprocal = 65536u / unsigned(total);
    auto channel = [&](uint8_t oldColor, uint8_t newColor) {
        const unsigned weighted = unsigned(oldColor) * unsigned(previous) +
                                  unsigned(newColor) * unsigned(contribution);
        return uint8_t(std::min(255u, (weighted * reciprocal) >> 16));
    };
    sample = {channel(sample.r, color.r), channel(sample.g, color.g),
              channel(sample.b, color.b), uint8_t(total)};
}
// A8310/A8470 and the eight-octant expansion in A9100/A9480. A blocked
// predecessor contributes 16, otherwise its propagated value is used. The
// destination's own collision does not black out the wall's visible face.
class LightOcclusion {
    static constexpr int side = maximumLightRadius * 2 + 1;
    std::array<uint8_t, side * side> blocked_{};
    std::array<uint8_t, side * side> propagated_{};
    int centerX_, centerY_;
    static size_t index(int x, int y) {
        return size_t(y + maximumLightRadius) * side + size_t(x + maximumLightRadius);
    }
    int predecessor(int x, int y) const {
        const auto at = index(x, y);
        return blocked_[at] ? 16 : propagated_[at];
    }
    void propagate(int x, int y, int sourceX, int sourceY) {
        const int dx = sourceX - ((centerX_ + x) * lightPrecision + lightPrecision / 2);
        const int dy = sourceY - ((centerY_ + y) * lightPrecision + lightPrecision / 2);
        const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        const int ax = std::abs(dx), ay = std::abs(dy);
        int value = 0;
        if (!ax && !ay) return;
        if (!ay) value = predecessor(x + sx, y);
        else if (!ax) value = predecessor(x, y + sy);
        else if (ax >= ay) {
            const int weight = (ay << 8) / ax;
            value = (predecessor(x + sx, y) * (256 - weight) +
                     predecessor(x + sx, y + sy) * weight) >> 8;
        } else {
            const int weight = (ax << 8) / ay;
            value = (predecessor(x, y + sy) * (256 - weight) +
                     predecessor(x + sx, y + sy) * weight) >> 8;
        }
        propagated_[index(x, y)] = uint8_t(value);
    }

  public:
    LightOcclusion(const Grid &grid, int sourceX, int sourceY, int radius)
        : centerX_(sourceX >> 3), centerY_(sourceY >> 3) {
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x) {
                const int gx = centerX_ + x, gy = centerY_ + y;
                const bool outside = gx < 0 || gy < 0 || gx >= grid.width || gy >= grid.height;
                const size_t at = outside ? 0 : size_t(gy) * grid.width + gx;
                blocked_[index(x, y)] = outside || grid.lightBlocked[at] || grid.objectLightBlocked[at];
            }
        // The native kernel starts at ring 2; its central 3x3 is the seed.
        for (int ring = 2; ring <= radius; ++ring)
            for (int step = 0; step <= ring; ++step) {
                propagate(-step, -ring, sourceX, sourceY);
                propagate( step, -ring, sourceX, sourceY);
                propagate(-step,  ring, sourceX, sourceY);
                propagate( step,  ring, sourceX, sourceY);
                propagate( ring, -step, sourceX, sourceY);
                propagate( ring,  step, sourceX, sourceY);
                propagate(-ring, -step, sourceX, sourceY);
                propagate(-ring,  step, sourceX, sourceY);
            }
    }
    int at(int x, int y) const {
        return propagated_[index(x - centerX_, y - centerY_)];
    }
};
} // namespace
Color sceneLightColor(uint32_t nativeColor) {
    // D2Client 1.13c 189A0 -> A86C0: zero resets white; otherwise RRGGBB.
    return nativeColor ? Color{uint8_t(nativeColor >> 16), uint8_t(nativeColor >> 8), uint8_t(nativeColor), 255} : WHITE;
}
struct LightingView::Sources {
    struct Light { int radius; uint64_t seen{}; };
    std::map<std::tuple<uint64_t,int,uint32_t>,Light> overlays;
    uint64_t visit{};
    double remainder{};
    bool update{};
    void begin(float elapsed) {
        ++visit;
        remainder += double(std::max(0.f, elapsed)) * 25;
        const auto frames = std::floor(remainder + .000001);
        remainder = std::max(0.0, remainder - frames);
        // Client 44B2F increments its 25 Hz counter; 8B1AC -> A9E30
        // updates lights once when that counter changed, even after a gap.
        update = frames > 0;
    }
};
LightingView::LightingView() : sources_(std::make_unique<Sources>()) {}
void LightingView::resizeMask(int width, int height) {
    // Keep capacity when panels close/open or fractional camera positions
    // change the required span by one sample; do not recreate it every frame.
    width = std::max(width, maskWidth_); height = std::max(height, maskHeight_);
    if (width == maskWidth_ && height == maskHeight_) return;
    if (lightMap_.id) UnloadTexture(lightMap_);
    maskWidth_ = width; maskHeight_ = height;
    lightPixels_.resize(size_t(width) * height);
    cornerPixels_.resize(lightPixels_.size());
    Image image = GenImageColor(width, height, BLACK);
    lightMap_ = LoadTextureFromImage(image);
    UnloadImage(image);
    if (lightMap_.id) {
        SetTextureFilter(lightMap_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(lightMap_, TEXTURE_WRAP_CLAMP);
    }
}
LightingView::~LightingView() {
    if (lightMap_.id) UnloadTexture(lightMap_);
}
void LightingView::resetEnvironment() {
    environments_ = {};
    frameRemainder_ = 0;
    eclipse_ = false;
    denIntensity_.reset();
    sanctumIntensity_.reset();
    cloakLevel_=-1;cloakFactor_=1;
}
void LightingView::resetSources() { *sources_ = {}; }
int LightingView::overlayRadius(uint64_t owner, int overlay, float startedAt, int initial, int target) {
    // 6B49F -> A8C20/A8BE0: no light for a zero target or a failed
    // nonpositive initial allocation. Identity includes this incarnation.
    if (initial <= 0 || target <= 0) return 0;
    initial = std::min(initial, maximumLightRadius);
    target = std::min(target, maximumLightRadius);
    auto entry = sources_->overlays.try_emplace(
        std::tuple{owner,overlay,std::bit_cast<uint32_t>(startedAt)}, Sources::Light{initial}).first;
    auto &light = entry->second;
    if (light.seen != sources_->visit && sources_->update) {
        // A9BB0: exactly +/-8 internal units, i.e. one subtile. This is
        // gated by the client clock, never by GPU frame count or lerp age.
        light.radius += light.radius < target ? 1 : light.radius > target ? -1 : 0;
    }
    light.seen = sources_->visit;
    return light.radius;
}
void LightingView::finishSources() {
    std::erase_if(sources_->overlays,[&](const auto &entry) { return entry.second.seen != sources_->visit; });
}
void LightingView::beginCloak(int duration,int level) {
    // D2Client 1.13c StateSet2: five ticks down, 3/5 duration dark,
    // 2/5 back up (25 if zero). Ambient only; unit lights survive.
    cloakLevel_=level;cloakFrame_=cloakAngle_=0;cloakFactor_=1;
    cloakRise_=2*duration/5;if(!cloakRise_) cloakRise_=25;
    cloakTotal_=3*duration/5+cloakRise_+5;
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
    sources_->begin(dt);
    if (dt <= 0 || level.act < 0 || level.act >= int(environments_.size())) return;
    frameRemainder_ += dt * 25.f;
    const int frames = int(frameRemainder_ + .000001f);
    frameRemainder_ = std::max(0.f, frameRemainder_ - frames);
    auto &environment = environments_[size_t(level.act)];
    const bool eclipse = level.act == 1 && eclipse_;
    const int rate = eclipse ? 4 : timeRate;
    const auto &cycles = eclipse ? eclipseCycles : level.act == 3 ? actFourCycles : normalCycles;
    for (int frame = 0; frame < frames; ++frame) {
        if(cloakLevel_!=level.id || cloakFrame_>cloakTotal_) {cloakLevel_=-1;cloakFactor_=1;}
        if(cloakLevel_>=0) {
            if(cloakFrame_<5 || cloakFrame_>=cloakTotal_-cloakRise_) {
                cloakFactor_=std::clamp(float(std::cos(double(cloakAngle_)*3.14159265358979323846/256.0)),0.f,1.f);
                cloakAngle_=cloakFrame_<5?cloakFrame_*128/5:128-(cloakFrame_-cloakTotal_+cloakRise_)*128/cloakRise_;
            } else cloakFactor_=0;
            ++cloakFrame_;
        }
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
void LightingView::prepare(const Grid &grid, const LevelRecord &level, Vec player, int radius,
                          Color playerColor, std::span<const SceneLight> lights,
                          std::span<const SceneLightRegion> regions, Rectangle worldBounds) {
    // Cover the inverse-projected viewport, including the neighbours used by
    // corner averaging and bilinear sampling. The old fixed 61x61 field cut
    // across expanded viewports and abruptly fell back to ambient light.
    const int originX = int(std::floor(worldBounds.x)) - 2;
    const int originY = int(std::floor(worldBounds.y)) - 2;
    const int endX = int(std::ceil(worldBounds.x + worldBounds.width)) + 2;
    const int endY = int(std::ceil(worldBounds.y + worldBounds.height)) + 2;
    resizeMask(endX - originX + 1, endY - originY + 1);
    if (!lightMap_.id) return;
    origin_ = {float(originX), float(originY)};
    // A9A20: a nonzero room RGB selects the table, including intensity zero.
    // A cloak's cached ambient applies to the near rooms too; unit lights
    // remain independent. Preserve intensity until the palette/shader boundary.
    const auto roomAmbient = [&](const LevelRecord &roomLevel) {
        const auto &source = cloakLevel_ >= 0 ? level : roomLevel;
        const auto &environment = environments_[size_t(std::clamp(source.act, 0, 4))];
        const bool tableLight = source.lightRed || source.lightGreen || source.lightBlue;
        Color result = tableLight
            ? Color{uint8_t(source.lightRed), uint8_t(source.lightGreen), uint8_t(source.lightBlue),
                    uint8_t(std::clamp(source.lightIntensity, 0, 255))}
            : Color{environment.color.r, environment.color.g, environment.color.b,
                    uint8_t(std::clamp(environment.intensity, 0, 255))};
        // A9A9A -> 524D0: Den overrides room-table light before the
        // normal fallback. Completion removes this override, not unit lights.
        if(source.id==8 && denIntensity_)
            result={255,64,48,uint8_t(std::clamp(*denIntensity_,0,255))};
        if((source.id==107 || source.id==108) && sanctumIntensity_)
            result={255,64,48,uint8_t(std::clamp(*sanctumIntensity_,0,255))};
        result.a = uint8_t(float(result.a) * cloakFactor_);
        return result;
    };
    const Color ambient = roomAmbient(level);
    ambient_ = ambient;
    std::fill(lightPixels_.begin(), lightPixels_.end(), ambient);
    // A9C90 fills with the observer room, then overwrites each near-room
    // footprint. Prepared room bounds exclude the drawable DS1 overlap edge.
    for (const auto &region : regions) {
        const Color roomLight = roomAmbient(*region.level);
        const int left = std::max(0, region.x - originX), top = std::max(0, region.y - originY);
        const int right = std::min(maskWidth_, region.x + region.width - originX);
        const int bottom = std::min(maskHeight_, region.y + region.height - originY);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                lightPixels_[size_t(y) * maskWidth_ + x] = roomLight;
    }
    auto add = [&](const SceneLight &source) {
        if (source.radius <= 0) return;
        const int sourceRadius = std::min(source.radius, maximumLightRadius);
        const int sourceX = lightCoordinate(source.position.x), sourceY = lightCoordinate(source.position.y);
        const int cellX = sourceX >> 3, cellY = sourceY >> 3;
        const int left = std::max(0, cellX - sourceRadius - originX);
        const int top = std::max(0, cellY - sourceRadius - originY);
        const int right = std::min(maskWidth_, cellX + sourceRadius - originX + 1);
        const int bottom = std::min(maskHeight_, cellY + sourceRadius - originY + 1);
        if (left >= right || top >= bottom) return;
        std::optional<LightOcclusion> occlusion;
        if (source.occlusion == SceneLight::Occlusion::Propagated)
            occlusion.emplace(grid, sourceX, sourceY, sourceRadius);
        const int fixedRadius = sourceRadius * lightPrecision;
        const int falloff = (255 << 16) / fixedRadius;
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {
                const size_t index = size_t(y) * maskWidth_ + x;
                const int gx = originX + x, gy = originY + y;
                const int distance = lightDistance(sourceX - gx * lightPrecision, sourceY - gy * lightPrecision);
                if (distance >= fixedRadius) continue;
                int contribution = ((fixedRadius - distance) * falloff) >> 16;
                if (occlusion) {
                    const int obstruction = occlusion->at(gx, gy);
                    if (obstruction >= 16) continue;
                    contribution = (contribution * (8 - (obstruction >> 1))) >> 3;
                }
                accumulateLight(lightPixels_[index], contribution, source.color);
            }
    };
    // This view uses the native high-quality player path (A9C1F -> A9480),
    // which is selected by light quality, not Levels.IsInside.
    add({player, std::clamp(radius, 1, maximumLightRadius), playerColor, SceneLight::Occlusion::Propagated});
    for (const auto &source : lights) add(source);
    // D2Gfx CmnSubtile.cpp's high-quality tile path takes the integer mean
    // of four neighbouring light samples at each subtile corner before
    // Gouraud interpolation. Keep that neighbourhood averaging in this
    // view adapter too; raw radial samples expose sharp contour boundaries.
    const auto sample = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < maskWidth_ && y < maskHeight_
            ? lightPixels_[size_t(y) * maskWidth_ + x] : ambient;
    };
    for (int y = 0; y < maskHeight_; ++y)
        for (int x = 0; x < maskWidth_; ++x) {
            const auto a = sample(x - 1, y - 1), b = sample(x, y - 1);
            const auto c = sample(x - 1, y), d = sample(x, y);
            cornerPixels_[size_t(y) * maskWidth_ + x] = {
                uint8_t((int(a.r) + b.r + c.r + d.r) >> 2),
                uint8_t((int(a.g) + b.g + c.g + d.g) >> 2),
                uint8_t((int(a.b) + b.b + c.b + d.b) >> 2),
                uint8_t((int(a.a) + b.a + c.a + d.a) >> 2)};
        }
    UpdateTexture(lightMap_, cornerPixels_.data());
}
Color LightingView::sample(Vec position) const {
    // D2Client A8230: a unit/wall sample truncates the eighth-subtile input
    // and clamps to the light field. It does not sample at the sprite's top.
    if (lightPixels_.empty()) return ambient_;
    const int x = std::clamp(int(std::floor(position.x - origin_.x)), 0, maskWidth_ - 1);
    const int y = std::clamp(int(std::floor(position.y - origin_.y)), 0, maskHeight_ - 1);
    return lightPixels_[size_t(y) * maskWidth_ + x];
}
void LightingView::bind(PaletteBlendView &palette, Vec player, Vec playerScreen, float zoom, bool enabled) const {
    palette.setLighting(lightMap_, player, playerScreen, origin_, zoom, ambient_, !enabled);
}
} // namespace d2x
