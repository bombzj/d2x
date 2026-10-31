#pragma once
#include "content/content_preparation.hpp"
#include "contracts/online.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/graphics/primitives.hpp"
#include "world/map.hpp"
#include "gameplay/items/handle.hpp"
#include "presentation/world/world_input_view.hpp"
#include <memory>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace d2x {
class SceneView;
class RemoteCombat;
class RemoteUiClients;
struct RemoteMapDisplayState {
    bool visible{}, large{true}, right{true};
    bool running{true}; // Session preference survives terrain/act renderer replacement.
    Vec offset;
    struct QuestEffects {
        uint64_t gameGeneration{},denOccurrence{};
        uint64_t forgeOccurrence{};
        uint64_t nihlathakOccurrence{};
        uint64_t anyaOccurrence{};
        uint64_t sanctumOccurrence{},sanctumEndOccurrence{};
        bool sanctumLight{};
        float sanctumEndFrames{-1};
        std::map<OnlineUnitKey,uint64_t> hiddenUnits;
        float denFrames{-1};
        float seconds{};
        bool denComplete{};
        struct DenRoom {uint64_t revision{};float born{};std::vector<Vec> positions;};
        std::map<std::array<int,3>,DenRoom> denRooms;
    } questEffects;
};
struct RemoteSceneFrame {
    WorldInputView input;
    EntityId hoveredMonster; // Display selection is independent of the selected skill.
    std::vector<size_t> visibleMapTiles;
    Vec observationExtent;
};
// Original-resource presentation of a server replica. Returns hit/projection results; owns no input gestures.
class RemoteScene {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    RemoteScene(Archives &, int palette, RemoteMapDisplayState &, ContentPreparation = ContentPreparation::OriginalRules);
    ~RemoteScene();
    RemoteSceneFrame frame(const OnlineView &, const Map &, const OnlineSceneView &,
                            SceneView &, const RemoteCombat &, const RemoteUiClients &, bool uiConsumed, Vec mouse, bool rightHand, bool paused);
    // Called after the controller validates/releases/acquires this frame's gesture.
    void drawTargetInfo(const OnlineView &, const RemoteSceneFrame &, SceneView &, const RemoteCombat &, EntityId lockedTarget) const;
    int renderedUnits() const;
    int unavailableUnits() const;
    bool playerDisplayed() const;
    bool castInputBusy() const;
    std::optional<Vec> playerDisplayPosition(const OnlineView &) const;
    std::optional<Vec> playerPathPosition(const OnlineView &) const;
    const std::vector<OnlinePlayerDisplay> &players() const;
    const std::vector<OnlineSceneView::MissileDisplay> &missileObservations() const;
    std::vector<std::string> effectLimitations() const;
    void effectStatus(OnlineSceneView &) const;
};
} // namespace d2x
