#pragma once
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/graphics/primitives.hpp"
#include "world/map.hpp"
#include "gameplay/items/handle.hpp"
#include "presentation/input.hpp"
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
struct RemoteSceneIntent {
    std::optional<OnlinePoint> move;
    std::optional<OnlineUnitKey> interact;
    std::optional<OnlineCombatCommand> combat;
    std::optional<ItemHandle> pickup;
    std::vector<size_t> visibleMapTiles;
    bool run{true}, leave{}, stopCombat{};
};
// Original-resource presentation of a server replica. Emits intents only.
class RemoteScene {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    RemoteScene(Archives &, int palette, RemoteMapDisplayState &);
    ~RemoteScene();
    RemoteSceneIntent frame(const OnlineView &, const Map &, const OnlineSceneView &,
                            SceneView &, const RemoteCombat &, bool uiConsumed, const FrameInput &);
    int renderedUnits() const;
    int unavailableUnits() const;
    bool playerDisplayed() const;
    std::vector<std::string> effectLimitations() const;
    void combatSubmitted(bool accepted);
};
} // namespace d2x
