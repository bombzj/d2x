#pragma once
#include "contracts/map.hpp"
#include "gameplay/areas/intents.hpp"

namespace d2x {
class IMapClient {
  public:
    virtual ~IMapClient() = default;
    // Bound observer only. Borrows end on the next corresponding read/destruction.
    virtual const MapSceneView &read() const = 0;
    virtual const TravelMenuView &travel(EntityId source, int act) const = 0;
    virtual bool waypointSource(EntityId object) const = 0;
    virtual void submit(MapIntent intent) = 0;
};
} // namespace d2x
