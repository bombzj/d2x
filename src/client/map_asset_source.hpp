#pragma once
#include "world/identity.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace d2x {
struct MapData;
struct Tile;
struct MapAssetMarker { int objectClass = -1; std::string npcClass; };
// A local resource borrow, separate from gameplay messages and network packets.
// No grid, dynamic objects, seeds, archive handle, or GPU resources are exposed.
struct MapAssetView {
    RegionId region = RegionId::Encampment;
    bool loaded = false, townAutomap = false;
    int palette = 0, levelType = 0, variant = 0;
    const MapData *data = nullptr;
    std::vector<const Tile *> tiles;
    std::vector<MapAssetMarker> markers;
    std::vector<std::string> propKeys;
};
class IMapAssetSource {
  public:
    virtual ~IMapAssetSource() = default;
    virtual size_t size() const = 0;
    // Decoded data/tiles remain valid for this source's lifetime. No unloading yet.
    virtual const MapAssetView &readAsset(size_t slot) const = 0;
};
} // namespace d2x
