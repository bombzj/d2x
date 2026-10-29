#pragma once
#include "core/math.hpp"
#include <array>

namespace d2x {
// Native 1.13c unit glyph and BH Crosshook use the same twelve segments.
inline constexpr std::array<Vec, 13> automapUnitOutline{{{0,-1}, {2,-2}, {4,-1}, {2,0}, {4,1}, {2,2},
    {0,1}, {-2,2}, {-4,1}, {-2,0}, {-4,-1}, {-2,-2}, {0,-1}}};
// Value-only map projection. Camera/clip policy remains with the map renderer;
// the BH overlay and native terrain share its origin without sharing anchors.
class AutomapProjection {
    Vec origin_, pan_;
    int divisor_;
    bool large_;
  public:
    AutomapProjection(int width, int height, bool large, Vec miniOrigin,
                      float sideShift, Vec observer, Vec pan);
    Vec cell(Vec world) const;
    Vec bhPoint(Vec nativePosition) const;
};
} // namespace d2x
