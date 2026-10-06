#pragma once
#include "client/automap_exploration.hpp"
#include "input.hpp"
#include "client/character_client.hpp"
#include "client/quest_client.hpp"
#include "client/npc_client.hpp"
#include "client/map_client.hpp"
#include "client/map_asset_source.hpp"
#include "presentation/inventory/inventory_panel.hpp"
#include "presentation/world/lighting_view.hpp"
#include "presentation/world/palette_blend_view.hpp"
#include "scene_assets.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "presentation/world/preset_pops.hpp"
#include "presentation/world/world_draw_view.hpp"
#include "gameplay/model/events.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <algorithm>
#include <span>

namespace d2x {
struct Enemy;
struct PlayerCorpse;
class CombatEffectSet;
class IActorClient;
struct SpecialItemRecord;
enum class AutomapFade { No, Everything, Auto, Center };
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
    bool help = false, automap = false, debug = false, travelMenu = false;
    bool miniPanelOpen = false;
    bool gameMenuOpen = false;
    int gameMenuPage = 0;
    int gameMenuSelected = 2, gameMenuPressed = -1;
    float gameMenuTime = 0;
    Vec gameMenuMouse;
    bool minimapRight = false;
    bool automapLarge = false;
    bool automapNames = true;
    bool automapCenterWhenCleared = true, automapParty = true;
    AutomapFade automapFade = AutomapFade::Auto;
    Vec automapOffset;
    bool characterOpen = false;
    std::optional<bool> pointButtonPressed;
    bool hirelingOpen = false, hireListOpen = false;
    int hireListScroll = 0;
    bool questOpen = false;
    int questAct = 0;
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
    int waypointAct = 0;
    float animationTime = 0, heroTime = 0, stepClock = 0;
    float cainPortalAnimationStarted = -1;
    std::string heroMode = "nu", dialogue, dialogueSpeaker, dialogueStatus;
    std::optional<uint32_t> dialogueTextTopic;
    std::deque<NpcDialogueStarted> pendingNpcDialogue;
    EntityId dialogueObject;
    EntityId inventoryQuestNpc;
    NpcMenuAction inventoryNpcAction = NpcMenuAction::Imbue;
    EntityId orificeObject;
    std::optional<ItemHandle> orificeItem;
    std::vector<std::string> dialogueLines;
    float dialogueOffset = 0;
    bool dialogueManualScroll = false;
    size_t dialogueGossipTurn = 0;
    std::map<std::string, size_t> npcGossipTurns;
    std::string lootNotice;
    float noticeTime = 0;
    bool noticeError = false;
    bool capturesWorldInput() const { return gameMenuOpen || help || npcMenu || shopOpen || hireListOpen || !dialogue.empty(); }
    bool blocksInput() const { return capturesWorldInput() || travelMenu; }
};
class SceneView {
    Archives &archives_;
    const GameSession *session_ = nullptr;
    const GameSession &localSession() const;
    const IActorClient &actorClient_;
    IInventoryClient &inventoryClient_;
    InventoryView inventoryView_;
    ICharacterClient &characterClient_;
    CharacterView characterView_;
    IQuestClient &questClient_;
    QuestView questView_;
    INpcClient &npcClient_;
    IMapClient &mapClient_;
    const IMapAssetSource *mapAssets_ = nullptr;
    mutable MapSceneView mapView_;
    NpcConversationView npcView_;
    NpcSceneView npcScene_;
    mutable ShopView shopView_;
    mutable HirelingView hirelingView_;
    mutable HirelingListView hirelingListView_;
    SceneAssets assets_;
    int itemGroundPalette_ = -1;
    LightingView lighting_;
    PaletteBlendView paletteBlend_;
    std::array<std::unique_ptr<PaletteBlendView>, 5> actPaletteBlends_;
    Shader highlightShader_{};
    int highlightTransform_ = -1;
    UiPainter painter_;
    UiPainter speechPainter_;
    ViewState view_;
    struct QuestCompletionAnimation {
        enum class Phase { Idle, Pending, Playing };
        Phase phase = Phase::Idle;
        bool completed = false;
        float elapsed = 0;
    };
    std::array<QuestCompletionAnimation, size_t(QuestId::Count)> questAnimations_{};
    void resetQuestAnimations();
    void queueQuestAnimation(QuestId quest, bool completed);
    void advanceQuestAnimations(float dt);
    std::map<EntityId, float> landingAge_;
    using ClientMissile = ClientMissileVisual;
    std::vector<ClientMissile> clientMissiles_;
    std::map<EntityId, int> arcVisualFrames_;
    uint64_t projectileVisualRandom_ = 0;
    uint64_t nextClientMissile_ = 0;
    void createMissileImpactVisuals(int missileId, Vec position);
    void createIceShatter(Vec position, int size);
    void createBlizzardFall(int missileId, Vec position);
    void advanceMissileVisuals(float dt);
    void syncMissileAudio();
    std::map<EntityId, Vec> monsterPositions_, monsterLooks_;
    std::map<EntityId, float> nextMonsterFootstep_, nextMonsterNeutral_;
    std::set<EntityId> movingMonsters_;
    AutomapExploration exploredAutomap_;
    std::map<RegionId, std::vector<float>> roofOpacity_;
    std::map<RegionId, PresetPops> nativePops_;
    PresetPops worldPops_;
    uint64_t worldGameGeneration_ = ~uint64_t{}, worldAreaGeneration_ = ~uint64_t{};
    const Sprite *objectSprite(const WorldObject &object, RegionId region) const;
    Vec objectScreen(const WorldObject &object, Vec regionOffset = {}) const;
    struct LootLabel {
        ItemHandle item;
        std::string text;
        Rectangle bounds;
        Vec ground;
        Color color;
    };
    std::vector<LootLabel> lootLabels(Vec mouse) const;
    const Sprite *groundItemSprite(const ItemInstance &item) const;
    Rectangle lootBounds(const ItemInstance &item) const;
    void drawGroundItem(EntityId item, bool highlighted) const;
    void drawLootLabels(Vec mouse) const;
    void drawSelectableSprite(const Sprite *image, Vec position, bool highlighted,
                              Color tint = WHITE, Vector2 highlight = {2.f, 1.f}) const;
    void drawWorldLighting(const WorldDrawView &, std::span<const WorldDrawItem>);
    void drawNpcAlert(EntityId npc, Vec at, bool back) const;
    void drawShrineOverlays(int code, Vec at, int height, bool back) const;
    void drawPlayerShrineOverlay(Vec at, bool back) const;
    void drawCombatStateOverlays(const CombatEffectSet &effects, Vec screenPosition, int height, bool back) const;
    void drawPanelFrame(bool right) const;
    void drawMinimap(bool large) const;
    void revealAutomap();
    std::vector<AutomapVisibleCell> visibleAutomapCells() const;
    void drawHud() const;
    void drawGameMenu() const;
    void drawNpcDialogue() const;
    void displayNpcDialogue(EntityId object, std::string speaker, std::string text);
    void advanceNpcDialogue(float dt);
    void drawNpcMenu(Vec mouse) const;
    void drawNpcShop(Vec mouse) const;
    void drawWaypointMenu(Vec mouse) const;
    void drawControlPanel() const;
    void drawSkillControls(Vec mouse) const;
    void drawSkillIcon(std::optional<int> skill, Rectangle bounds, bool picker = false) const;
    void drawSkillTree(Vec mouse) const;
    void drawQuests(Vec mouse) const;
    void drawHelp() const;
    void drawExitHint(Vec mouse) const;
    void drawObjectHint(Vec mouse) const;
    void drawInventory(Vec mouse) const;
    void drawCharacter(Vec mouse) const;
    void drawHireling(Vec mouse) const;
    void drawHirelingPortrait() const;
    void drawHirelingList(Vec mouse) const;
    void drawStorage(Vec mouse) const;
    void drawCube(Vec mouse) const;
    void drawOrifice(Vec mouse) const;
    void drawContainerGrid(const ContainerGrid &grid, Vec mouse) const;
    void drawBelt(Vec mouse) const;
    void drawItemTooltip(const ItemInstance &item, Vec anchor,
                         std::optional<unsigned> price = {}, bool gamble = false,
                         std::string_view priceLabel = "Cost") const;
    void drawItemTooltip(const InventoryItemView &item, Vec anchor,
                         std::optional<unsigned> price = {},
                         std::string_view priceLabel = "Cost") const;
    void drawItemText(std::vector<ItemTextLine> lines, ItemQuality quality, Vec anchor,
                      std::optional<unsigned> price, std::string_view priceLabel) const;
    std::optional<unsigned> inventoryVendorPrice(ItemHandle item) const;
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
    void drawItemIcon(const InventoryItemView &item, Rectangle bounds, Color tint = WHITE) const;
    void drawItemArt(const std::string &key, const std::string &code, Rectangle bounds, Color tint) const;
    bool drawInventoryCursor(Vec mouse) const;

  public:
    Rectangle worldViewport() const;
        Rectangle gameMenuItemBounds(int index) const;
        int gameMenuAt(Vec mouse) const;
        int gameMenuItemCount() const;
    Rectangle hirelingSlotBounds(size_t index) const;
    bool hirelingPortraitVisible() const;
    std::optional<int> miniPanelAt(Vec mouse) const;
    const ExitView *exitAt(Vec mouse) const;
    SceneView(Archives &archives, const GameSession &session, const IActorClient &actorClient,
              IInventoryClient &inventoryClient, ICharacterClient &characterClient,
              IQuestClient &questClient, INpcClient &npcClient, IMapClient &mapClient, const IMapAssetSource &mapAssets);
    SceneView(Archives &, const ClassicData &, const IActorClient &, IInventoryClient &,
              ICharacterClient &, IQuestClient &, INpcClient &, IMapClient &);
    bool multiplayer() const { return !session_; }
    bool playOriginalCombatSound(std::string_view name, uint64_t frame) {
        return assets_.playOriginalCombatSound(name, frame);
    }
    void drawUi(Vec mouse) const;
    // Shared original-resource effects; server replica supplies presentation data only.
    void drawNativeIceShatter(Vec position, int size) { createIceShatter(position,size); }
    const SceneAssets::ProjectileVisual *projectileVisual(int id) { return assets_.ensureProjectile(id); }
    const SkillOverlayVisual *overlayVisual(int id) { return assets_.ensureOverlay(id); }
    void drawMissile(int id, Vec position, Vec heading, float age, float remaining, Vec pixelOffset = {}) const;
    bool launchClientMissile(int id, Vec start, Vec target, int level, float delay = 0,
                             std::optional<float> remaining = {}, int pathIndex = -1,
                             EntityId owner = {}, bool hostile = false, int pierce = 0);
    void advanceClientMissiles(float dt, const Grid &, Vec origin, std::span<const ClientMissileTarget>);
    void clearClientMissiles() { clientMissiles_.clear(); arcVisualFrames_.clear(); assets_.audio.resetEmitters(); }
    const auto &clientMissiles() const { return clientMissiles_; }
    std::optional<int> weaponMissile(std::string_view code) const {
        const auto found = assets_.weaponMissiles.find(code);
        return found == assets_.weaponMissiles.end() ? std::nullopt : std::optional{found->second};
    }
    Vec clientMissilePosition(const ClientMissile &, const Grid &, Vec origin) const;
    void cancelPendingClientMissiles(EntityId owner) {
        std::erase_if(clientMissiles_, [&](const auto &value) { return value.flight && value.age < 0 && value.owner == owner; });
    }
    void drawSpellOverlay(int id, Vec position, float age, bool loop, int height = 1) const;
    void drawGroundLabels(Vec mouse) const { drawLootLabels(mouse); }
    void drawDeathNotice() const;
    void drawCorpseLabel(const std::string &label, Vec screenPosition) const;
    void drawHighlightedActor(const Sprite *image, Vec position) const { drawSelectableSprite(image, position, true); }
    void drawNpcQuestAlert(EntityId npc, Vec position, bool back) const { drawNpcAlert(npc, position, back); }
    void drawAutomap(const AutomapDrawView &) const;
    std::vector<size_t> drawWorld(const WorldDrawView &);
    const Sprite *groundItemSprite(const InventoryItemView &) const;
    void drawGroundItem(const InventoryItemView &, bool highlighted) const;
    void drawEnemyBar(std::string_view title, std::optional<float> life, std::string_view description = {}, Color color = WHITE) const;
    void refreshUi(float dt);
    ~SceneView();
    ViewState &ui() { return view_; }
    const ViewState &ui() const { return view_; }
    const InventoryView &inventoryView() const { return inventoryView_; }
    void refreshInventory();
    const CharacterView &characterView() const { return characterView_; }
    void refreshCharacterView();
    const QuestView &questView() const { return questView_; }
    const NpcConversationView &npcView() const { return npcView_; }
    const MapSceneView &mapView() const;
    const ShopView &shopView() const;
    const HirelingView &hirelingView() const;
    const HirelingListView &hirelingListView() const;
    void refreshInteractions();
    void refreshNpcView(EntityId npc);
    std::vector<std::pair<RegionId, size_t>> automapLayers() const;
    Vec screen(Vec position) const;
    Vec world(Vec position) const;
    void drawInteractionLabel(const std::string &, Vec, int offset = 70) const;
    bool visible(const WorldObject &object) const;
    const WorldObject *objectAt(Vec mouse) const;
    const PlayerCorpse *playerCorpseAt(Vec mouse) const;
    const Sprite *playerCorpseSprite(const PlayerCorpse &corpse) const;
    const std::string &heroAppearanceError() const { return assets_.heroAppearanceError(); }
    bool leftSkillAllowed(int skill) const;
    struct SkillPickerSlot { std::optional<int> skill; Rectangle bounds; };
    std::vector<SkillPickerSlot> skillPickerSlots(bool right) const;
    std::optional<int> skillAt(Vec mouse) const;
    std::optional<ItemHandle> lootAt(Vec mouse, bool labelsOnly = false) const;
    void advance(float dt);
    void advanceUi(float dt);
    void pauseDebugPresentation(bool paused) { assets_.audio.pauseEmitters(paused); }
    void notice(std::string text, bool error = false);
    void openNpcDialogue(EntityId object, std::string speaker, std::string text);
    void cancelNpcDialogue();
    void openNpcMenu(EntityId object, std::string speaker, bool firstIntroduction);
    bool startNpcTalk();
    bool startNpcIntroduction();
    bool startNpcTopic(QuestId quest);
    bool startNpcTextTopic(uint32_t topic);
    bool openNpcShop(bool gamble = false);
    void closeNpcShop();
    bool npcShopDropAt(Vec mouse) const;
    NpcMenuSelection clickNpcMenu(Vec mouse);
    void scrollNpcDialogue(int amount);
    bool closeNpcDialogue();
    std::optional<uint32_t> clickNpcShop(Vec mouse, bool directBuy = false);
    std::optional<RegionId> clickWaypointMenu(Vec mouse);
    void scrollNpcShop(int pages);
    bool showNextNpcGossip();
    void sessionRestored();
    const AutomapLayers &automapExploration() const { return exploredAutomap_.layers(); }
    uint64_t automapContentFingerprint() const { return assets_.automapContentFingerprint; }
    bool restoreAutomapExploration(AutomapLayers layers);
    void collectMapVariants(Archives &archives);
    std::vector<TravelEntryView> travelEntries() const;
};
} // namespace d2x
