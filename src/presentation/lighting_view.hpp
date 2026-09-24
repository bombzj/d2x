#pragma once
#include "content/world_catalog.hpp"
#include "core/id.hpp"
#include "core/math.hpp"
#include "raylib.h"
#include "world/navigation.hpp"
#include "world/region.hpp"
#include <vector>

namespace d2x {
// View-only light mask. The collision grid supplies visibility; no lighting state is saved.
class LightingView {
    static constexpr int maskRadius = 21;
    static constexpr int maskSide = maskRadius * 2 + 1;
    Shader shader_{};
    Texture2D visibility_{};
    std::vector<Color> pixels_;
    RegionId cachedRegion_{};
    int cachedX_ = -1, cachedY_ = -1, cachedRadius_ = -1;
    int originX_ = 0, originY_ = 0;

  public:
    LightingView();
    ~LightingView();
    LightingView(const LightingView &) = delete;
    LightingView &operator=(const LightingView &) = delete;
    void update(const Grid &grid, RegionId region, Vec player, int radius);
    void draw(const LevelRecord &level, Vec player, Vec playerScreen, float zoom, int radius,
              const std::vector<WorldObject> &objects) const;
};
} // namespace d2x
