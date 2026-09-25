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
        releaseAfterLoad_ = true;
    }
    Vec movement() const { return movement_; }
    bool temporaryRun() const { return temporaryRun_; }
};
} // namespace d2x
