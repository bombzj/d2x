#pragma once
#include "input.hpp"
#include "scene_view.hpp"

namespace d2x {
class SceneController {
    GameSession &session_;
    SceneView &view_;
    float repeatClick_ = 0;
    bool pickupClick_ = false;
    bool inventoryClick_ = false;
    bool inventoryRight_ = false;
    bool releaseAfterLoad_ = false;
    bool skillGesture_ = false;
    int channelInputSkill_ = -1;
    EntityId leftCombatTarget_, rightCombatTarget_;
    std::optional<int> leftTargetSkill_, rightTargetSkill_;
    RegionId inputRegion_ = session_.state().area.region;
    Vec movement_;
    bool temporaryRun_ = false;
    void click(Vec mouse);
    bool handleInventory(const FrameInput &input);
    bool handleSkills(const FrameInput &input);
    void toggleInventory();
    bool queueInventory(GameCommand command, EntityId source);

  public:
    SceneController(GameSession &session, SceneView &view) : session_(session), view_(view) {}
    bool handle(const FrameInput &input, float elapsed);
    void resetInput() {
        repeatClick_ = 0;
        pickupClick_ = inventoryClick_ = inventoryRight_ = false;
        movement_ = {};
        temporaryRun_ = false;
        skillGesture_ = false;
        view_.ui().pointButtonPressed.reset();
        view_.ui().questPressed = -1;
        channelInputSkill_ = -1;
        leftCombatTarget_ = rightCombatTarget_ = {};
        leftTargetSkill_.reset();
        rightTargetSkill_.reset();
        inputRegion_ = session_.state().area.region;
        releaseAfterLoad_ = true;
    }
    Vec movement() const { return movement_; }
    EntityId combatTarget() const {
      if (inputRegion_ != session_.state().area.region || session_.state().player.dead) return {};
      if (rightCombatTarget_)
        return view_.ui().rightSkill == rightTargetSkill_ ? rightCombatTarget_ : EntityId{};
      return view_.ui().leftSkill == leftTargetSkill_ ? leftCombatTarget_ : EntityId{};
    }
    bool temporaryRun() const { return temporaryRun_; }
};
} // namespace d2x
