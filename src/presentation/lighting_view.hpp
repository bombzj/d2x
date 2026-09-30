#pragma once
#include "content/world_catalog.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "gameplay/model/definitions.hpp"
#include "raylib.h"
#include "palette_blend_view.hpp"
#include "world/navigation.hpp"
#include <array>
#include <vector>
#include <span>

namespace d2x {
struct SceneLight {
    Vec position;
    float radius = 0;
    Color color{0, 0, 0, 255};
};
// View-only light mask. DT1 and object-mode flags supply visibility; no lighting state is saved.
class LightingView {
    static constexpr int maskRadius = 30;
    static constexpr int maskSide = maskRadius * 2 + 1;
    Texture2D lightMap_{};
    std::vector<Color> pixels_;
    mutable std::vector<Color> lightPixels_;
    // Transient presentation state, initialized as ENVIRONMENT_AllocDrlgEnvironment.
    struct Environment {
        int ticks = 0, cycle = 2, intensity = 128;
        Color color{255, 255, 255, 255};
    };
    std::array<Environment, 5> environments_{};
    float frameRemainder_ = 0;
    RegionId cachedRegion_{};
    int cachedX_ = -1, cachedY_ = -1, cachedRadius_ = -1;
    uint64_t cachedObstacleRevision_ = 0;
    int originX_ = 0, originY_ = 0;

  public:
    LightingView();
    ~LightingView();
    LightingView(const LightingView &) = delete;
    LightingView &operator=(const LightingView &) = delete;
    void invalidate() { cachedRadius_ = -1; }
    void advance(float dt, const LevelRecord &level);
    void resetEnvironment();
    void update(const Grid &grid, const LevelRecord &level, RegionId region, Vec player, int radius);
    void draw(const PaletteBlendView &palette, const LevelRecord &level, Vec player, Vec playerScreen,
              float zoom, int radius, std::span<const SceneLight> lights) const;
};
} // namespace d2x
