#include "navigation.hpp"
#include <algorithm>
#include <limits>

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
    int x = int(std::floor(point.x)), y = int(std::floor(point.y));
    if (x >= 0 && y >= 0 && x < width_ && y < height_) {
        int index = cells_[y * width_ + x];
        if (index >= 0)
            return index;
    }
    // DS1's shared extra row/column belongs to the closest authored room.
    int best = -1, distance = std::numeric_limits<int>::max();
    for (int i = 0; i < int(rooms_.size()); ++i) {
        const auto &r = rooms_[i];
        int d = std::max({r.x - x, x - r.x - r.width, 0}) + std::max({r.y - y, y - r.y - r.height, 0});
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
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
    return r.x <= s.x + s.width && s.x <= r.x + r.width && r.y <= s.y + s.height && s.y <= r.y + r.height;
}
} // namespace d2x
