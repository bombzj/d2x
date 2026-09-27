#pragma once
#include "input.hpp"
#include "inventory_panel.hpp"
#include "lighting_view.hpp"
#include "scene_assets.hpp"
#include <cstdint>
#include <deque>
#include <map>

namespace d2x {
struct ViewState {
    InventoryUi inventory;
    std::optional<int> leftSkill, rightSkill;
    std::array<std::optional<int>, 2> weaponLeftSkills{}, weaponRightSkills{};
    unsigned displayedWeaponSet = 0;
    std::optional<bool> skillPicker; // false: left button, true: right button
    bool skillTreeOpen = false;
    int skillPage = 3;
    std::string skillClass;
    Vec camera, clickAt;
    EntityId combatTarget;
    float clickAge = 10, zoom = 1;
    bool help = false, automap = false, debug = false, pause = false, travelMenu = false;
    bool miniPanelOpen = false;
    bool gameMenuOpen = false;
    int gameMenuSelected = 2, gameMenuPressed = -1;
    float gameMenuTime = 0;
    Vec gameMenuMouse;
    bool minimapRight = false;
    bool automapLarge = false;
    bool automapNames = true;
    Vec automapOffset;
    bool characterOpen = false;
    std::optional<bool> pointButtonPressed;
    bool hirelingOpen = false, hireListOpen = false;
    int hireListScroll = 0;
    bool questOpen = false;
    int questSelected = -1;
    int questPressed = -1;
    bool questNotice = false;
    int questUpdated = -1;
    std::optional<unsigned> lastDenRemaining;
    bool showLoot = false, shopOpen = false, npcMenu = false;
    bool shopGamble = false;
    bool shopRepair = false;
    bool npcTopics = false;
    int shopPage = 0;
    int shopCategory = 0;
    std::optional<uint32_t> shopConfirm;
    std::optional<ItemHandle> shopSalePending;
    EntityId waypointSource;
    int travelPage = 0;
    float animationTime = 0, heroTime = 0, stepClock = 0;
    uint64_t portalRevision = 0;
    float portalAnimationStarted = -1;
    float cainPortalAnimationStarted = -1;
    std::string heroMode = "nu", dialogue, dialogueSpeaker, dialogueStatus;
    std::deque<NpcDialogueStarted> pendingNpcDialogue;
    EntityId dialogueObject;
    EntityId imbueNpc;
    std::vector<std::string> dialogueLines;
    float dialogueOffset = 0;
    bool dialogueManualScroll = false;
    size_t dialogueGossipTurn = 0;
    std::map<std::string, size_t> npcGossipTurns;
    std::string lootNotice;
    float noticeTime = 0;
    bool noticeError = false;
    bool blocksWorld() const { return gameMenuOpen || pause || travelMenu || help || npcMenu || shopOpen || hireListOpen || !dialogue.empty(); }
};
class SceneView {
    const GameSession &session_;
    SceneAssets assets_;
    LightingView lighting_;
    Shader highlightShader_{};
    UiPainter painter_;
    UiPainter speechPainter_;
    ViewState view_;
    struct QuestCompletionAnimation {
        enum class Phase { Idle, Pending, Playing };
        Phase phase = Phase::Idle;
        bool completed = false;
        float elapsed = 0;
    };
    std::array<QuestCompletionAnimation, size_t(ActOneQuest::Count)> questAnimations_{};
    void resetQuestAnimations();
    void queueQuestAnimation(ActOneQuest quest, uint32_t stage);
    void advanceQuestAnimations(float dt);
    std::map<EntityId, float> landingAge_;
    std::map<EntityId, Vec> monsterPositions_, monsterLooks_;
    std::map<EntityId, float> nextMonsterFootstep_, nextMonsterNeutral_;
    std::set<EntityId> movingMonsters_;
    std::map<RegionId, std::vector<uint8_t>> exploredAutomap_;
    std::map<RegionId, std::vector<float>> roofOpacity_;
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
    void drawGroundItem(EntityId item, bool highlighted) const;
    void drawLootLabels(Vec mouse) const;
    void drawTerrain() const;
    void drawActors(Vec mouse) const;
    void drawSelectableSprite(const Sprite *image, Vec position, bool highlighted,
                              Color tint = WHITE) const;
    void drawMagic() const;
    void drawLighting() const;
    void drawNpcAlerts() const;
    void drawPanelFrame(bool right) const;
    void drawMinimap(bool large) const;
    void revealAutomap();
    void drawHud() const;
    void drawGameMenu() const;
    void drawNpcDialogue() const;
    void displayNpcDialogue(EntityId object, std::string speaker, std::string text);
    void advanceNpcDialogue(float dt);
    Rectangle worldViewport() const;
    void drawNpcMenu(Vec mouse) const;
    void drawNpcShop(Vec mouse) const;
    void drawWaypointMenu(Vec mouse) const;
    void drawControlPanel() const;
    void drawSkillControls(Vec mouse) const;
    void drawSkillIcon(std::optional<int> skill, Rectangle bounds) const;
    void drawSkillTree(Vec mouse) const;
    void drawQuests(Vec mouse) const;
    void drawHelp() const;
    void drawExitHint(Vec mouse) const;
    void drawObjectHint(Vec mouse) const;
    void drawInventory(Vec mouse) const;
    void drawCharacter(Vec mouse) const;
    void drawHireling(Vec mouse) const;
    void drawHirelingList(Vec mouse) const;
    std::string hirelingName(std::string_view key) const;
    void drawStorage(Vec mouse) const;
    void drawCube(Vec mouse) const;
    void drawContainerGrid(const ContainerGrid &grid, Vec mouse) const;
    void drawBelt(Vec mouse) const;
    void drawItemTooltip(const ItemInstance &item, Vec anchor,
                         std::optional<unsigned> price = {}, bool gamble = false,
                         std::string_view priceLabel = "Cost") const;
    const SpecialItemRecord *specialItem(const ItemInstance &item) const;
    std::string itemName(const ItemInstance &item) const;
    static Color itemColor(ItemQuality quality);
    struct VisibleMonster {
        const Enemy *enemy;
        Vec position;
        int region;
    };
    std::vector<VisibleMonster> visibleMonsters() const;
    void itemButton(Rectangle bounds, const char *label, Color color) const;
    void drawItemIcon(const ItemInstance &item, Rectangle bounds, Color tint = WHITE) const;
    bool drawInventoryCursor(Vec mouse) const;
    void orb(bool mana, float fraction) const;

  public:
        Rectangle gameMenuItemBounds(int index) const;
        int gameMenuAt(Vec mouse) const;
    Rectangle hirelingSlotBounds(size_t index) const;
    std::optional<int> miniPanelAt(Vec mouse) const;
    const LevelExit *exitAt(Vec mouse) const;
    SceneView(Archives &archives, const GameSession &session);
    ~SceneView();
    ViewState &ui() { return view_; }
    const ViewState &ui() const { return view_; }
    std::vector<std::pair<RegionId, size_t>> automapLayers() const;
    Vec screen(Vec position) const;
    Vec world(Vec position) const;
    bool visible(const WorldObject &object) const;
    const WorldObject *objectAt(Vec mouse) const;
    const std::string &heroAppearanceError() const { return assets_.heroAppearanceError(); }
    bool leftSkillAllowed(int skill) const;
    std::vector<std::optional<int>> skillChoices(bool right) const;
    std::optional<int> skillAt(Vec mouse) const;
    std::optional<ItemHandle> lootAt(Vec mouse, bool labelsOnly = false) const;
    void toggleMute() { assets_.audio.muted = !assets_.audio.muted; }
    void advance(float dt);
    void advanceUi(float dt);
    void draw(Vec mouse) const;
    void notice(std::string text, bool error = false);
    void openNpcDialogue(EntityId object, std::string speaker, std::string text);
    void cancelNpcDialogue();
    void openNpcMenu(EntityId object, std::string speaker, bool firstIntroduction);
    bool startNpcTalk();
    bool startNpcIntroduction();
    bool startNpcTopic(ActOneQuest quest);
    bool openNpcShop(bool gamble = false);
    void closeNpcShop();
    bool npcShopDropAt(Vec mouse) const;
    int clickNpcMenu(Vec mouse);
    void scrollNpcDialogue(int amount);
    bool closeNpcDialogue();
    std::optional<uint32_t> clickNpcShop(Vec mouse, bool directBuy = false);
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
