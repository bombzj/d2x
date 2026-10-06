#pragma once
#include "input.hpp"
#include "world/world_input_view.hpp"
#include "gameplay/items/intents.hpp"

namespace d2x {
class IActorClient;
class IInventoryClient;
class ICharacterClient;
class INpcClient;
class IMapClient;
struct InventoryItemView;
class SceneView;
class SceneController {
    bool handleDeath(const FrameInput &);
    bool handlePanels(const FrameInput &, float);
    bool handleMenu(const FrameInput &);
    IActorClient &actorClient_;
    IInventoryClient &inventoryClient_;
    ICharacterClient &characterClient_;
    INpcClient &npcClient_;
    IMapClient &mapClient_;
    SceneView &view_;
    bool pickupClick_ = false;
    // UI owns a mouse press through release, even if its panel closes.
    bool inventoryClick_ = false;
    bool inventoryRight_ = false;
    bool releaseAfterLoad_ = false;
    bool skillGesture_ = false;
    enum class Gesture { None, Move, Interact, LeftCast, RightCast };
    Gesture gesture_ = Gesture::None;
    EntityId lockedTarget_;
    std::optional<InputSkillSelection> gestureSkill_;
    bool repeated_ = false, pendingMove_ = false, worldBlocked_ = true;
    float inputTime_ = 0, nextCast_ = 0, nextMove_ = 0;
    Vec gestureMouse_;
    std::optional<Vec> gesturePoint_;
    uint64_t gameGeneration_ = ~uint64_t{}, areaGeneration_ = ~uint64_t{};
    void cancelWorldGesture();
    bool temporaryRun_ = false;
    bool handleTravel(const FrameInput &input);
    void openGameMenu(Vec mouse);
    bool handleInventory(const FrameInput &input);
    bool handleSkills(const FrameInput &input);
    bool handleNpcMenu(const FrameInput &input);
    bool handleNpcDialogue(const FrameInput &input);
    bool handleNpcShop(const FrameInput &input);
    bool handleHirelingList(const FrameInput &input);
    bool handleHirelingPanel(const FrameInput &input);
    bool handleHirelingPortrait(const FrameInput &input);
    bool handleHirelingToggle(const FrameInput &input);
    bool handleQuestPress(const FrameInput &input);
    bool handleQuestToggle(const FrameInput &input);
    bool handleQuestPanel(const FrameInput &input);
    void toggleInventory();
    bool queueInventory(InventoryIntent command, EntityId source);
    void submitNpcItemService(ItemHandle item);
    void endInventoryNpcConversation(EntityId npc);

  public:
    SceneController(IActorClient &, IInventoryClient &, ICharacterClient &, INpcClient &, IMapClient &, SceneView &);
    bool uiConsumed() const { return worldBlocked_ || inventoryClick_ || inventoryRight_ || pickupClick_ || skillGesture_ || releaseAfterLoad_; }
    bool handle(const FrameInput &input, float elapsed);
    void handleWorld(const FrameInput &, const WorldInputView &, float elapsed);
    void resetInput();
    void discardBufferedInput(bool focused);
    EntityId combatTarget() const { return lockedTarget_; }
    bool temporaryRun() const { return temporaryRun_; }
};
} // namespace d2x
