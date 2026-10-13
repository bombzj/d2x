#include "navigation.hpp"
#include <algorithm>

namespace d2x {
RoomLayout::RoomLayout(int width, int height, std::vector<RoomBounds> rooms)
    : width_(width), height_(height), rooms_(std::move(rooms)), cells_(size_t(width) * height, -1) {
    // Complete DS1 presets use the original 8-tile DRLG subdivision. Generated
    // mazes/outdoors already supply their authored room footprints.
    if (rooms_.empty())
        for (int y = 0; y < height; y += 40)
            for (int x = 0; x < width; x += 40)
                rooms_.push_back({x, y, std::min(40, width - x), std::min(40, height - y), true});
    for (int i = 0; i < int(rooms_.size()); ++i) {
        const auto &r = rooms_[i];
        for (int y = r.y; y < std::min(height, r.y + r.height); ++y)
            for (int x = r.x; x < std::min(width, r.x + r.width); ++x)
                cells_[y * width + x] = i;
    }
}
int RoomLayout::roomAt(Vec point) const {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
        point.x >= width_ || point.y >= height_) return -1;
    return cells_[size_t(int(std::floor(point.y))) * width_ + int(std::floor(point.x))];
}

const RoomBounds *RoomLayout::room(Vec point) const {
    const int index = roomAt(point);
    return index < 0 ? nullptr : &rooms_[size_t(index)];
}
std::vector<const RoomBounds *> RoomLayout::nearRooms(const RoomBounds &observer) const {
    std::vector<const RoomBounds *> near;
    // D2MOO DRLGROOM near-room list uses a gap below six game tiles on
    // each axis. RoomBounds are in subtiles (five per game tile).
    constexpr int nearGap = 6 * 5;
    for (const auto &candidate : rooms_) {
        const int gapX = std::max({observer.x - candidate.x - candidate.width,
                                   candidate.x - observer.x - observer.width, 0});
        const int gapY = std::max({observer.y - candidate.y - candidate.height,
                                   candidate.y - observer.y - observer.height, 0});
        if (gapX < nearGap && gapY < nearGap)
            near.push_back(&candidate);
    }
    return near;
}
bool RoomLayout::nearby(Vec observer, Vec point) const {
    int a = roomAt(observer), b = roomAt(point);
    if (a < 0 || b < 0)
        return false;
    const auto &r = rooms_[a], &s = rooms_[b];
    // The same-level DRLG near-room criterion, shared with nearRooms().
    const int gapX=std::max({r.x-s.x-s.width,s.x-r.x-r.width,0});
    const int gapY=std::max({r.y-s.y-s.height,s.y-r.y-r.height,0});
    return gapX<6*5 && gapY<6*5;
}
} // namespace d2x
