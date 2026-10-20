#pragma once
#include "room_layout.hpp"
#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace d2x {
struct RoomTerrainId {
    int level{}, x{}, y{}; // Original global tile anchor.
    auto operator<=>(const RoomTerrainId &) const = default;
};
struct RoomTerrainLink {
    RoomTerrainId id;
    int width{}, height{};
    bool town{};
    std::vector<RoomTerrainId> near;
};
struct RoomTerrainTile {
    int array{}, x{}, y{}, type{};
    uint32_t flags{};
    std::string library;
    size_t record{};
    std::array<uint8_t,25> collision{};
};
struct RoomTerrainSample {
    int width{}, height{}; // Subtiles, no drawable overlap row.
    std::vector<uint16_t> collision;
    RoomPopulationLayout population;
    uint64_t tileRandom{};
    std::optional<uint64_t> initialActiveRandom;
    std::vector<RoomTerrainTile> tiles;
    std::vector<RoomTerrainId> activeNear;
};
class PreparedRoomTerrain;
// Pure generation state over already decoded immutable inputs. No file/MPQ,
// image, device, actor or transport access is allowed through this interface.
// The authoritative world owns one session per act and supplies DRLG events.
class RoomTerrainSession {
  public:
    virtual ~RoomTerrainSession() = default;
    virtual void include(const PreparedRoomTerrain &) = 0;
    virtual void prepare(RoomTerrainId) = 0;
    virtual void create(RoomTerrainId) = 0;
    virtual void release(RoomTerrainId) = 0;
    virtual std::optional<RoomTerrainSample> sample(RoomTerrainId, bool tiles = false,
        std::span<const std::pair<int,int>> openedTombTiles = {}) const = 0;
};
class PreparedRoomTerrain {
  public:
    virtual ~PreparedRoomTerrain() = default;
    virtual int act() const = 0;
    virtual std::span<const RoomTerrainLink> rooms() const = 0;
    virtual std::unique_ptr<RoomTerrainSession> instantiate() const = 0;
};
} // namespace d2x
