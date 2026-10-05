#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/graphics/primitives.hpp"
#include "world/map.hpp"
#include <memory>

namespace d2x {
struct RemoteSceneIntent {
    std::optional<OnlinePoint> move;
    bool run{true}, leave{};
};
// Original-resource presentation of a server replica. Emits intents only.
class RemoteScene {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit RemoteScene(Archives &);
    ~RemoteScene();
    RemoteSceneIntent frame(const OnlineView &, const Map &, const OnlineSceneView &, Vec mouse);
    int renderedUnits() const;
    int unavailableUnits() const;
    bool playerDisplayed() const;
};
} // namespace d2x
