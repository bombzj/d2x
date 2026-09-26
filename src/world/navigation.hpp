#pragma once
#include "core/bytes.hpp"
#include "core/math.hpp"
#include <deque>
namespace d2x {
struct RoomBounds {
    int x, y, width, height;
    bool populate;
};
// Immutable spatial room index. Activity is derived from the observer's room and
// touching neighbours, following DRLG's InRoom / InSight / OutOfSight model.
class RoomLayout {
    int width_ = 0, height_ = 0;
    std::vector<RoomBounds> rooms_;
    std::vector<int> cells_;
    int roomAt(Vec point) const;

  public:
    RoomLayout() = default;
    RoomLayout(int width, int height, std::vector<RoomBounds> rooms);
    const RoomBounds *room(Vec point) const;
    bool nearby(Vec observer, Vec point) const;
};
struct Grid {
    int width = 0, height = 0;
    Bytes blocked;
    Bytes lightBlocked;
    Grid() = default;
    Grid(int w, int h) : width(w), height(h), blocked(size_t(w) * h), lightBlocked(size_t(w) * h) {}
    bool walkable(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && !blocked[y * width + x];
    }
    bool walkable(Vec p) const { return walkable(int(std::floor(p.x)), int(std::floor(p.y))); }
    bool segment(Vec a, Vec b) const;
    bool lightSegment(Vec a, Vec b) const;
    Vec nearest(Vec p) const;
    // Scene inspection arrival: an interior point of the largest walkable component.
    // Real level transitions must use the original linked warp coordinates instead.
    Vec inspectionArrival() const;
    std::deque<Vec> path(Vec from, Vec to) const;
};
} // namespace d2x
