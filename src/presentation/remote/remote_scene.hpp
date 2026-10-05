#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/graphics/primitives.hpp"
#include "world/map.hpp"
#include <memory>

namespace d2x {
struct RemoteMapDisplayState {
    bool visible{}, large{true}, right{true};
    Vec offset;
};
struct RemoteSceneIntent {
    std::optional<OnlinePoint> move;
    std::optional<OnlineUnitKey> interact;
    std::optional<OnlineWaypointDestination> waypoint;
    std::vector<size_t> visibleMapTiles;
    bool run{true}, leave{}, closeWaypoint{};
};
// Original-resource presentation of a server replica. Emits intents only.
class RemoteScene {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    RemoteScene(Archives &, int palette, RemoteMapDisplayState &);
    ~RemoteScene();
    RemoteSceneIntent frame(const OnlineView &, const Map &, const OnlineSceneView &, Vec mouse);
    int renderedUnits() const;
    int unavailableUnits() const;
    bool playerDisplayed() const;
};
} // namespace d2x
