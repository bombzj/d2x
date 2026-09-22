#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include <deque>
namespace d2x {
struct Grid {
    int width = 0, height = 0;
    Bytes blocked;
    Grid() = default;
    Grid(int w, int h) : width(w), height(h), blocked(size_t(w) * h) {}
    bool walkable(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && !blocked[y * width + x];
    }
    bool walkable(Vec p) const { return walkable(int(std::floor(p.x)), int(std::floor(p.y))); }
    bool segment(Vec a, Vec b) const;
    Vec nearest(Vec p) const;
    // Scene inspection arrival: an interior point of the largest walkable component.
    // Real level transitions must use the original linked warp coordinates instead.
    Vec inspectionArrival() const;
    std::deque<Vec> path(Vec from, Vec to) const;
};
} // namespace d2x
