#pragma once
#include "core/math.hpp"
#include <tuple>

namespace d2x {
// D2MOO UNITS_InitializeStaticPath: objects/items use a subtile corner;
// dynamic units retain their precise (initially half-subtile) coordinates.
inline Vec staticUnitPosition(Vec point) {
    return {std::floor(point.x), std::floor(point.y)};
}
struct SceneOrder {
    int pass = 1, diagonal = 0;
    float depth = 0;
    int layer = 0;
    bool operator<(const SceneOrder &other) const {
        return std::tie(pass, diagonal, depth, layer) <
               std::tie(other.pass, other.diagonal, other.depth, other.layer);
    }
};
inline SceneOrder sceneOrder(Vec feet, int pass = 1, bool wall = false, int layer = 0) {
    // Diablerie Iso.SortingOrder / WorldRenderer and OpenDiablo2 renderPass3:
    // terrain cell diagonals first, then units by their ground anchor inside
    // that diagonal. Walls precede units in their cell. Texture bounds and
    // animation offsets must never become a unit's painter depth.
    const int diagonal = int(std::floor(feet.x / 5)) + int(std::floor(feet.y / 5));
    return {pass, diagonal, wall ? float(diagonal * 5) : feet.x + feet.y, layer};
}
} // namespace d2x
