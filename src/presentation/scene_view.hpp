#pragma once
#include "input.hpp"
#include "inventory_panel.hpp"
#include "scene_assets.hpp"

namespace d2x {
struct ViewState {
    InventoryUi inventory;
    std::array<Skill, hotbarSlots> hotbar{Skill::Fireball, Skill::FrostNova, Skill::Whirlwind,
                                          Skill::Teleport, Skill::Leap,      Skill::WarCry};
    std::optional<Skill> leftSkill, rightSkill = Skill::Fireball;
    std::optional<bool> skillPicker; // false: left button, true: right button
    Vec camera, clickAt;
    float clickAge = 10, zoom = 1;
    bool help = false, automap = false, debug = false, pause = false, travelMenu = false;
    bool showLoot = false, shopOpen = false, npcMenu = false;
    int shopPage = 0;
    int shopCategory = 0;
    std::optional<uint32_t> shopConfirm;
    EntityId waypointSource;
    int travelPage = 0;
    float animationTime = 0, heroTime = 0, stepClock = 0;
    uint64_t portalRevision = 0;
    float portalAnimationStarted = -1;
    std::string heroMode = "nu", dialogue, dialogueSpeaker, dialogueStatus;
    EntityId dialogueObject;
    std::vector<std::string> dialogueLines;
    int dialogueScroll = 0;
    size_t dialogueGossipTurn = 0;
    std::string lootNotice;
    float noticeTime = 0;
    bool noticeError = false;
    bool blocksWorld() const { return pause || travelMenu || help || npcMenu || shopOpen || !dialogue.empty(); }
};
class SceneView {
    const GameSession &session_;
    SceneAssets assets_;
    UiPainter painter_;
    UiPainter speechPainter_;
    ViewState view_;
    std::map<EntityId, float> landingAge_;
    std::map<EntityId, Vec> monsterPositions_, monsterLooks_;
    std::set<EntityId> movingMonsters_;
    const Sprite *objectSprite(const WorldObject &object, RegionId region) const;
    struct LootLabel {
        ItemHandle item;
        std::string text;
        Rectangle bounds;
        Vec ground;
        Color color;
    };
    std::vector<LootLabel> lootLabels(Vec mouse) const;
    Rectangle lootBounds(const ItemInstance &item) const;
    void drawGroundItem(EntityId item) const;
    void drawLootLabels(Vec mouse) const;
    void drawTerrain() const;
    void drawActors() const;
    void drawMagic() const;
    void drawMinimap(bool large) const;
    void drawHud() const;
    void drawNpcDialogue() const;
    void drawNpcMenu() const;
    void drawNpcShop(Vec mouse) const;
    void drawWaypointMenu(Vec mouse) const;
    void drawControlPanel() const;
    void drawSkillControls(Vec mouse) const;
    void drawSkillIcon(std::optional<Skill> skill, Rectangle bounds) const;
    void drawHelp() const;
    void drawExitHint(Vec mouse) const;
    void drawInventory(Vec mouse) const;
    void drawStorage(Vec mouse) const;
    void drawContainerGrid(const ContainerGrid &grid, Vec mouse) const;
    void drawBelt(Vec mouse) const;
    void drawItemTooltip(const ItemInstance &item, Vec anchor) const;
    const SpecialItemRecord *specialItem(const ItemInstance &item) const;
    std::string itemName(const ItemInstance &item) const;
    void itemButton(Rectangle bounds, const char *label, Color color) const;
    void drawItemIcon(const ItemInstance &item, Rectangle bounds, Color tint = WHITE) const;
    void drawInventoryCursor(Vec mouse) const;
    void orb(bool mana, float fraction) const;

  public:
    const LevelExit *exitAt(Vec mouse) const;
    SceneView(Archives &archives, const GameSession &session);
    ViewState &ui() { return view_; }
    const ViewState &ui() const { return view_; }
    Vec screen(Vec position) const;
    Vec world(Vec position) const;
    bool visible(const WorldObject &object) const;
    const WorldObject *objectAt(Vec mouse) const;
    const std::string &heroAppearanceError() const { return assets_.heroAppearanceError(); }
    bool leftSkillAllowed(Skill skill) const { return assets_.skillIcons.at(size_t(skill)).leftAllowed; }
    std::vector<std::optional<Skill>> skillChoices(bool right) const;
    std::optional<ItemHandle> lootAt(Vec mouse, bool labelsOnly = false) const;
    void toggleMute() { assets_.audio.muted = !assets_.audio.muted; }
    void advance(float dt);
    void draw(Vec mouse) const;
    void notice(std::string text, bool error = false);
    void openNpcDialogue(EntityId object, std::string speaker, std::string text);
    void openNpcMenu(EntityId object, std::string speaker);
    bool startNpcTalk();
    bool openNpcShop();
    int clickNpcMenu(Vec mouse);
    void scrollNpcDialogue(int amount);
    void closeNpcDialogue();
    std::optional<uint32_t> clickNpcShop(Vec mouse);
    std::optional<RegionId> clickWaypointMenu(Vec mouse);
    void scrollNpcShop(int pages);
    bool showNextNpcGossip();
    void sessionRestored();
    void collectMapVariants(Archives &archives) {
        assets_.collectMapVariants(archives, session_.worldContent(), session_.monsterContent());
    }
    std::vector<WorldEntry> travelEntries() const;
};
std::string playerAnimationMode(const PlayerState &player);
} // namespace d2x
