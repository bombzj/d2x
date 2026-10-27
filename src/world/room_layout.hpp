#pragma once
#include "core/math.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2x {
struct RoomBounds {
    int x, y, width, height;
    bool populate{};
    bool checkLosDraw{true};
};
struct RoomPopulationRegion {
    int index{},left{},top{},right{},bottom{};
    bool node{};
};
struct NativeTileInfo {int index{},x{},y{};};
// Original global tile anchors of a reciprocal DRLG room-tile connection.
struct NativeWarpRooms {int sourceX{},sourceY{},destinationX{},destinationY{};};
struct RoomPopulationLayout {
    struct Floor {int x{},y{};uint16_t material{};};
    int x{},y{},width{},height{};
    bool single{};
    bool waypoint{},warp{};
    size_t allocationOrder{};
    std::optional<Vec> waypointTile;
    std::vector<RoomPopulationRegion> regions;
    std::vector<size_t> cells;
    std::vector<Floor> floors;
};
// Immutable spatial room index. Activity is derived from the observer's room and
// DRLG near-room list, following its same-level six-tile gap criterion.
class RoomLayout {
    int width_ = 0, height_ = 0;
    std::vector<RoomBounds> rooms_;
    std::vector<int> cells_;
    int roomAt(Vec point) const;

  public:
    RoomLayout() = default;
    RoomLayout(int width, int height, std::vector<RoomBounds> rooms);
    // Ownership never falls back to a nearby room outside its footprint.
    const std::vector<RoomBounds> &rooms() const { return rooms_; }
    const RoomBounds *room(Vec point) const;
    std::vector<const RoomBounds *> nearRooms(const RoomBounds &observer) const;
    bool nearby(Vec observer, Vec point) const;
};
} // namespace d2x
