#pragma once
#include "input.hpp"
#include "gameplay/model/commands.hpp"

namespace d2x {
class GameSession;
class SceneView;
class SceneController {
    GameSession &session_;
    SceneView &view_;
    float repeatClick_ = 0;
    bool pickupClick_ = false;
    // UI owns a mouse press through release, even if its panel closes.
    bool inventoryClick_ = false;
    bool inventoryRight_ = false;
    bool releaseAfterLoad_ = false;
    bool skillGesture_ = false;
    int channelInputSkill_ = -1;
    EntityId leftCombatTarget_, rightCombatTarget_;
    std::optional<int> leftTargetSkill_, rightTargetSkill_;
    RegionId inputRegion_;
    Vec movement_;
    bool temporaryRun_ = false;
    void click(Vec mouse);
    void openGameMenu(Vec mouse);
    bool handleInventory(const FrameInput &input);
    bool handleSkills(const FrameInput &input);
    void toggleInventory();
    bool queueInventory(GameCommand command, EntityId source);

  public:
    SceneController(GameSession &session, SceneView &view);
    bool handle(const FrameInput &input, float elapsed);
    void resetInput();
    Vec movement() const { return movement_; }
    EntityId combatTarget() const;
    bool temporaryRun() const { return temporaryRun_; }
};
} // namespace d2x
