#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/graphics/primitives.hpp"
#include "world/map.hpp"
#include "gameplay/items/handle.hpp"
#include "presentation/world/world_input_view.hpp"
#include <memory>
#include <string>
#include <vector>

namespace d2x {
class SceneView;
class RemoteCombat;
struct RemoteMapDisplayState {
    bool visible{}, large{true}, right{true};
    bool running{true}; // Session preference survives terrain/act renderer replacement.
    Vec offset;
};
struct RemoteSceneFrame {
    WorldInputView input;
    std::vector<size_t> visibleMapTiles;
};
// Original-resource presentation of a server replica. Returns hit/projection results; owns no input gestures.
class RemoteScene {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    RemoteScene(Archives &, int palette, RemoteMapDisplayState &);
    ~RemoteScene();
    RemoteSceneFrame frame(const OnlineView &, const Map &, const OnlineSceneView &,
                            SceneView &, const RemoteCombat &, bool uiConsumed, Vec mouse, bool rightHand, bool paused);
    int renderedUnits() const;
    int unavailableUnits() const;
    bool playerDisplayed() const;
    std::optional<Vec> playerDisplayPosition() const;
    const std::vector<OnlinePlayerDisplay> &players() const;
    std::vector<std::string> effectLimitations() const;
    void effectStatus(OnlineSceneView &) const;
};
} // namespace d2x
