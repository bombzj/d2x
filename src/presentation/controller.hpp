#pragma once
#include "input.hpp"
#include "gameplay/items/intents.hpp"

namespace d2x {
class GameSession;
class IActorClient;
class IInventoryClient;
class ICharacterClient;
class INpcClient;
struct InventoryItemView;
class SceneView;
class SceneController {
    GameSession &session_;
    IActorClient &actorClient_;
    IInventoryClient &inventoryClient_;
    ICharacterClient &characterClient_;
    INpcClient &npcClient_;
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
    bool handleNpcMenu(const FrameInput &input);
    bool handleNpcDialogue(const FrameInput &input);
    bool handleQuestPress(const FrameInput &input);
    bool handleQuestToggle(const FrameInput &input);
    bool handleQuestPanel(const FrameInput &input);
    void toggleInventory();
    bool queueInventory(InventoryIntent command, EntityId source);
    bool inventoryQuestTargetValid(EntityId object) const;
    bool openInventoryQuestTarget(Vec mouse, const InventoryItemView &item);
    void submitInventoryQuest(EntityId object, ItemHandle item);
    void submitImbue(ItemHandle item);
    void endInventoryNpcConversation(EntityId npc);

  public:
    SceneController(GameSession &session, IActorClient &actorClient, IInventoryClient &inventoryClient,
                    ICharacterClient &characterClient, INpcClient &npcClient, SceneView &view);
    bool handle(const FrameInput &input, float elapsed);
    void resetInput();
    Vec movement() const { return movement_; }
    EntityId combatTarget() const;
    bool temporaryRun() const { return temporaryRun_; }
};
} // namespace d2x
