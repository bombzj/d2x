#pragma once
#include "input.hpp"
#include "contracts/online_scene.hpp"
#include "presentation/audio/audio_settings.hpp"
#include "client/character_client.hpp"
#include "client/quest_client.hpp"
#include "client/npc_client.hpp"
#include "client/map_client.hpp"
#include "presentation/inventory/inventory_panel.hpp"
#include "scene_assets.hpp"
#include "presentation/hud/chat_view.hpp"
#include "presentation/hud/trade_invite_view.hpp"
#include "presentation/hud/party_view.hpp"
#include "presentation/world/scene_geometry.hpp"
#include "presentation/world/tile_visibility.hpp"
#include "presentation/world/weather_debug.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <algorithm>
#include <span>
#include <tuple>

namespace d2x {
class IActorClient;
struct OnlineView;
struct Grid;
struct Map;
class LightingView;
class WeatherView;
class PaletteBlendView;
class AutomapProjection;
struct WorldDrawView;
struct WorldDrawItem;
enum class AutomapFade { No, Everything, Auto, Center };
struct ViewState {
    InventoryUi inventory;
    std::optional<int> leftSkill, rightSkill;
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
    std::optional<int> miniPanelPressed; // -1: bottom toggle, otherwise semantic button ID.
    bool gameMenuOpen = false;
    bool chatInputOpen = false, messageLogOpen = false;
    bool playerTradeBlocking = false;
    bool playerTradeEditable = false;
    EscapeMenuPage gameMenuPage = EscapeMenuPage::Main;
    int gameMenuSelected = 2, gameMenuPressed = -1;
    float gameMenuTime = 0;
    Vec gameMenuMouse;
    int gameMenuControlScroll = 0;
    ControlBindings controls = defaultControlBindings(), controlDraft = controls;
    int controlSelected = -1, controlColumn = 0;
    bool controlCapturing = false;
    int gameMenuScrollPressed = -1;
    bool gameMenuScrollDragging = false;
    int gameMenuSliderDragging = -1;
    AudioSettings audio;
    bool lighting = true;
    bool standardResolution = false; // App-owned window mode; menu submits a change.
    bool minimapRight = false;
    bool automapLarge = false;
    bool automapNames = true;
    bool automapCenterWhenCleared = true, automapParty = true;
    AutomapFade automapFade = AutomapFade::Auto;
    Vec automapOffset;
    bool characterOpen = false;
    // -1 is the close button; other values are attribute indices / skill IDs.
    std::optional<int> characterPressed, skillTreePressed;
    std::optional<bool> pointButtonPressed;
    bool hirelingOpen = false, hireListOpen = false;
    bool portraitsVisible = true;
    EntityId hirelingPortraitHovered;
    float hirelingPortraitHoverTime = 0;
    bool hirelingPortraitCanFeed = false;
    int hireListScroll = 0;
    struct HirelingConfirmation { uint32_t slot; uint64_t offerRevision; };
    std::optional<HirelingConfirmation> hirelingConfirmation;
    bool questOpen = false;
    bool questAutoPreview = false, hidePointer = false;
    bool partyOpen = false;
    int questAct = 0;
    int questSelected = -1;
    int questPressed = -1;
    bool questNotice = false;
    int questUpdated = -1;
    bool showLoot = false, shopOpen = false, npcMenu = false;
    bool shopGamble = false;
    bool shopRepair = false, shopSell = false;
    bool npcTopics = false;
    int shopPage = 0;
    int shopCategory = 0;
    std::optional<uint32_t> shopConfirm;
    Vec shopConfirmAt;
    int shopConfirmChoice = 0;
    std::optional<ItemHandle> shopSalePending;
    uint64_t shopRejectionRevision{}, shopRejectionDismissed{};
    Vec shopRejectionAt;
    EntityId waypointSource;
    int waypointAct = 0;
    float animationTime = 0;
    std::string dialogue, dialogueSpeaker, dialogueStatus;
    std::optional<uint32_t> dialogueTextTopic;
    bool dialogueFromMenu{};
    EntityId dialogueObject;
    EntityId inventoryQuestNpc;
    EntityId orificeObject;
    std::optional<ItemHandle> orificeItem;
    NpcMenuAction inventoryNpcAction = NpcMenuAction::Imbue;
    std::vector<std::string> dialogueLines;
    float dialogueOffset = 0;
    bool dialogueManualScroll = false;
    size_t dialogueGossipTurn = 0;
    std::map<std::string, size_t> npcGossipTurns;
    std::string lootNotice;
    float noticeTime = 0;
    bool noticeError = false;
    // BH/D2Hackmap display features.
    // Session capability, supplied by app; preferences alone cannot enable BH.
    bool bhEnabled = false;
    bool bhRevealMap = false;
    bool bhAlwaysShowLoot = false;
    bool bhShowXpText = false;
    bool bhShowBeltStatus = false;
    bool bhForceLightRadius = false;
    bool bhShowPathLine = false;
    bool bhShowResistances = false;
    bool bhShowMonsters = false;
    bool bhShowEnchantments = false;
    bool bhShowChests = false;
    bool bhAutomapOnJoin = false;
    bool bhShowLkChestLines = false;
    bool bhQuickCast = false;
    static constexpr int bhSettingsCount = 13;
    // BH settings overlay (Ctrl+Z).
    bool bhSettingsOpen = false;
    int bhSettingsSelected = 0;
    bool capturesWorldInput(bool includePlayerTrade = true) const;
    bool blocksInput() const;
};
class SceneView {
    Archives &archives_;
    const IActorClient &actorClient_;
    IInventoryClient &inventoryClient_;
    InventoryView inventoryView_;
    ICharacterClient &characterClient_;
    CharacterView characterView_;
    IQuestClient &questClient_;
    QuestView questView_;
    INpcClient &npcClient_;
    IMapClient &mapClient_;
    mutable MapSceneView mapView_;
    NpcConversationView npcView_;
    using NpcSpeechClock = std::chrono::steady_clock;
    NpcSpeechClock::time_point npcSpeechOpened_;
    struct NpcScrollClock {
        uint32_t start, last, average = 0, interpolated = 0;
        uint16_t repeats = 0;
    };
    std::optional<NpcScrollClock> npcScrollClock_;
    std::optional<unsigned> akaraGossipChoice_;
    NpcSceneView npcScene_;
    mutable ShopView shopView_;
    mutable HirelingView hirelingView_;
    mutable HirelingListView hirelingListView_;
    SceneAssets assets_;
    std::unique_ptr<LightingView> lighting_;
    std::unique_ptr<WeatherView> weather_;
    std::optional<uint64_t> questAudioGameGeneration_;
    std::optional<uint64_t> narrativeGameGeneration_;
    uint64_t narrativeSequence_{};
    std::map<EntityId,int> npcSoundIdentities_;
    std::unique_ptr<PaletteBlendView> paletteBlend_;
    std::array<std::unique_ptr<PaletteBlendView>, 5> actPaletteBlends_;
    Shader highlightShader_{};
    int highlightTransform_ = -1;
    UiPainter painter_;
    ViewState view_;
    struct ScreenShake {double startMilliseconds{};int magnitude{},buildMilliseconds{},holdMilliseconds{},fadeMilliseconds{};};
    std::optional<ScreenShake> screenShake_;
    Vec screenShakeOffset_;
    uint64_t areaTitleGame_=~uint64_t{};
    int areaTitleLevel_=-1;
    mutable unsigned areaTitleFrames_=120;
    bool areaTitlePending_{};
    std::optional<std::tuple<int, int, bool, bool>> automapLayout_;
    ChatView chat_;
    OriginalMenu originalMenu_;
    TradeInviteView tradeInvite_;
    PartyView party_;
    bool incomingHostility_ = false;
    struct QuestCompletionAnimation {
        enum class Phase { Idle, Pending, Playing, Acknowledge };
        Phase phase = Phase::Idle;
        bool pending = false;
        float elapsed = 0;
    };
    std::array<QuestCompletionAnimation, size_t(QuestId::Count)> questAnimations_{};
    void resetQuestAnimations();
    void queueQuestAnimation(QuestId quest, bool pending);
    void advanceQuestAnimations(float dt);
    struct GroundAnimation { uint64_t revision = 0; float age = 0; bool flipSoundConsumed = false, landSoundConsumed = false; };
    std::map<EntityId, GroundAnimation> groundAnimations_;
    uint64_t groundGameGeneration_ = ~uint64_t{}, groundAreaGeneration_ = ~uint64_t{};
    void advanceGroundAnimations(float dt);
    using ClientMissile = ClientMissileVisual;
    std::vector<ClientMissile> clientMissiles_;
    std::vector<EntityId> clientMissileHiddenOwners_;
    std::vector<ClientMissileOverlay> clientMissileOverlays_;
    uint64_t projectileVisualRandom_ = 0;
    uint64_t nextClientMissile_ = 0;
    void createMissileImpactVisuals(int missileId, Vec position, uint64_t &missileRandom);
    void createIceShatter(Vec position, int size);
    void createBlizzardFall(int missileId, Vec position);
    TileVisibility worldVisibility_;
    uint64_t worldGameGeneration_ = ~uint64_t{}, worldAreaGeneration_ = ~uint64_t{};
    uint64_t weatherRepositionSequence_{};
    struct LootLabel {
        ItemHandle item;
        std::string text;
        Rectangle bounds;
        Vec ground;
        size_t fontIndex = 0;
    };
    std::vector<LootLabel> lootLabels(Vec mouse) const;
    void drawGroundItem(EntityId item, bool highlighted) const;
    void drawLootLabels(Vec mouse) const;
    void drawSelectableSprite(const Sprite *image, Vec position, bool highlighted,
                              Color tint = WHITE, Vector2 highlight = {2.f, 1.f},
                              const PaletteBlendView *lighting = nullptr, Color light = WHITE) const;
    PaletteBlendView &prepareWorldLighting(const WorldDrawView &, std::span<const WorldDrawItem>, Rectangle worldBounds);
    void drawNpcAlert(EntityId npc, Vec at, bool back) const;
    void drawPanelFrame(bool right) const;
    void drawPanelTiles(const GpuAnimation &, bool right, size_t first = 0) const;
    void drawGameMenu() const;
    void drawNpcDialogue() const;
    void displayNpcDialogue(EntityId object, std::string speaker, std::string text);
    void advanceNpcDialogue(float dt);
    unsigned chooseAkaraGossip();
    void drawNpcMenu(Vec mouse) const;
    void drawNpcShop(Vec mouse) const;
    void drawWaypointMenu(Vec mouse) const;
    void drawControlPanel() const;
    void drawDurabilityWarning() const;
    void drawHostilityWarning() const;
    void drawAreaTitle() const;
    void drawSkillControls(Vec mouse) const;
    void drawSkillIcon(std::optional<int> skill, Rectangle bounds, bool picker = false, uint32_t owner=UINT32_MAX) const;
    void drawBoundSkillBar(bool right, Rectangle anchor) const;
    void drawSkillTree(Vec mouse) const;
    void drawQuests(Vec mouse) const;
    void drawQuestNotice() const;
    void drawHelp() const;
    void drawExitHint(Vec mouse) const;
    void drawBhOverlay() const;
    void drawBhSettings() const;
    void drawBhAutomapLines(const AutomapDrawView &, const AutomapProjection &,
                           Rectangle clip, const PaletteBlendView &) const;
    void drawBhAutomapMarker(const AutomapDrawView::Marker &, const AutomapProjection &,
                            Rectangle clip, Vec miniOrigin, const PaletteBlendView &) const;
    void drawInventory(Vec mouse) const;
    void drawCharacter(Vec mouse) const;
    void drawHireling(Vec mouse) const;
    void drawHirelingPortrait(Vec mouse) const;
    void drawHirelingList(Vec mouse) const;
    void drawStorage(Vec mouse) const;
    void drawCube(Vec mouse) const;
    void drawOrifice(Vec mouse) const;
    void drawPlayerTrade(Vec mouse) const;
    void drawContainerGrid(const ContainerGrid &grid, Vec mouse) const;
    void drawInventoryDrop(const InventoryDrop &drop, Rectangle surface) const;
    void drawBelt(Vec mouse) const;
    void drawItemTooltip(const InventoryItemView &item, Vec anchor,
                         std::optional<unsigned> price = {},
                         std::string_view priceLabel = {}) const;
    void drawItemText(std::vector<ItemTextLine> lines, ItemQuality quality, Vec anchor,
                      std::optional<unsigned> price, std::string_view priceLabel) const;
    // NPC shop tooltips are collected while the shop panel draws and emitted at
    // the end of drawUi, so the inventory and character panels cannot cover them.
    struct DeferredTooltip {
        bool active = false;
        std::vector<ItemTextLine> lines;
        ItemQuality quality = ItemQuality::Normal;
        std::optional<unsigned> price;
        std::string priceLabel;
        Vec anchor{};
    };
    mutable DeferredTooltip deferredTooltip_;
    void deferTooltip(std::vector<ItemTextLine> lines, ItemQuality quality, Vec anchor,
                      std::optional<unsigned> price, std::string_view priceLabel) const;
    std::optional<unsigned> inventoryVendorPrice(ItemHandle item) const;
    static size_t itemTextFontIndex(ItemQuality, ItemTextTone);
    // Draws one tooltip/label line, honouring 0xFF 'c' colour escapes with the
    // original PL2 item-text fonts; defaultFont covers uncoloured runs.
    void drawColoredItemText(const std::string &text, size_t defaultFont, Rectangle bounds, int fontSize) const;
    void itemButton(Rectangle bounds, const char *label, Color color) const;
    void drawItemIcon(const InventoryItemView &item, Rectangle bounds, bool showSockets = false, float pixelScale = 0) const;
    void drawItemBackground(const InventoryItemView &item, Rectangle bounds, bool hovered) const;
    void drawItemArt(const std::string &key, Rectangle bounds) const;
    bool drawInventoryCursor(Vec mouse) const;

  public:
    Rectangle worldViewport() const;
        Rectangle worldRenderViewport() const;
        bool worldContains(Vec mouse) const;
    // Symmetric observation extent around the camera, in projected native subtile axes.
    Vec worldObservationExtent() const;
    Rectangle gameMenuItemBounds(int index) const;
    int gameMenuAt(Vec mouse) const;
    int gameMenuItemCount() const;
    bool gameMenuItemEnabled(int index) const;
    bool adjustGameMenuSound(const FrameInput &);
    void applyAudioSettings(const AudioSettings &);
    void scrollGameMenu(const FrameInput &);
    bool editGameMenuControls(const FrameInput &);
    Rectangle hirelingSlotBounds(size_t index) const;
    bool hirelingPortraitVisible() const;
    std::optional<int> miniPanelAt(Vec mouse) const;
    const ExitView *exitAt(Vec mouse) const;
    SceneView(Archives &, const ClassicData &, WeatherCycleState &, const IActorClient &, IInventoryClient &,
                ICharacterClient &, IQuestClient &, INpcClient &, IMapClient &, const AudioSettings & = {});
    void updateWorldAudio(float clock, std::vector<SoundActorView> &actors,
                          std::vector<PresentationSoundEvent> &events, Vec listener, Vec terrainOrigin);
    void updateAreaTitle(uint64_t game,int level,const std::optional<std::array<uint8_t,37>> &availability);
    void playUiSound(std::string_view sound);
    int soundPeriod() const;
    void observeWorldSounds(const OnlineView &);
    void greetNpc(EntityId npc);
    void playItemUse(const InventoryItemView &item);
    void playPlayerFootstep(int identity, bool running, int material, int level, uint64_t frame);
    void questMusic(std::string_view music,int endFrames,int resumeBlock);
    void beginWorldGame(uint64_t generation);
    WeatherDebugState debugWeather(std::optional<WeatherDebugMode> mode = {}, int density = 255);
    const std::set<std::string, std::less<>> &soundLimitations() const;
    void drawUi(Vec mouse) const;
    void updateChat(const OnlineSocialView &social,const OnlineSocialView &account);
    void updateParty(const OnlineSocialView &social, uint32_t self, uint64_t game, bool town);
    PartyIntent handleParty(const FrameInput &input);
    bool partyReady() const;
    bool storageNavigationReady() const;
    bool storageMaterialUpgradeReady() const;
    bool storageMaterialsReady() const;
    float partyPortraitOffset() const;
    const std::string &partyReason() const;
    ChatIntent handleChat(const FrameInput &input);
    void chatSent(bool accepted);
    void debugChatMessage(std::string_view text, bool error = false);
    const ChatView &chat() const;
    void updatePlayerTrade(const OnlinePlayerTrade &trade);
    PlayerTradeIntent handlePlayerTrade(const FrameInput &input);
    const TradeInviteView &tradeInvite() const;
    // Shared original-resource effects; server replica supplies presentation data only.
    void drawNativeIceShatter(Vec position, int size);
    void triggerWeatherLightning(float delay);
    const SceneAssets::ProjectileVisual *projectileVisual(int id);
    const SkillOverlayVisual *overlayVisual(int id);
    void drawMissile(int id, Vec position, Vec heading, float age, float remaining, Vec pixelOffset = {},
                     std::optional<int> nativeFrame={}, const PaletteBlendView *lighting = nullptr, Color light = WHITE) const;
    bool launchClientMissile(int id, Vec start, Vec target, int level, float delay = 0,
                             std::optional<float> remaining = {}, int pathIndex = -1,
                             EntityId owner = {}, bool hostile = false, int pierce = 0,
                               ClientMissileSource source = ClientMissileSource::Program,int slowPercent = 0,EntityId guidedTarget = {},int velocityBonus = 0,
                               ClientMissileInitialization initialization = {});
    void advanceClientMissiles(float dt, const Map &, Vec origin, std::span<const ClientMissileTarget>,
                              std::span<const ClientMissileAnchor> = {});
    void clearClientMissiles();
    const std::vector<ClientMissile> &clientMissiles() const;
    std::vector<EntityId> takeClientMissileHiddenOwners();
    std::vector<ClientMissileOverlay> takeClientMissileOverlays();
    void retireClientMissileRoom(const std::array<int,3> &room);
    std::optional<int> weaponMissile(std::string_view code) const;
    Vec clientMissilePosition(const ClientMissile &, const Grid &, Vec origin) const;
    void cancelPendingClientMissiles(EntityId owner);
    void drawSpellOverlay(int id, Vec position, float age, bool loop, int height = 1,
                          const PaletteBlendView *lighting = nullptr, Color light = WHITE,
                          std::optional<int> preciseFrame = {}, Vec pixelOffset = {}) const;
    void drawGroundLabels(Vec mouse) const;
    void drawDeathNotice() const;
    void drawCorpseLabel(const std::string &label, Vec ground) const;
    void drawHighlightedActor(const Sprite *image, Vec position) const;
    void drawNpcQuestAlert(EntityId npc, Vec position, bool back) const;
    void drawAutomap(const AutomapDrawView &);
    std::vector<size_t> drawWorld(const WorldDrawView &,OnlineSceneView::MapRendering *diagnostics=nullptr);
    const ActorAnimation *actorAnimation(const ActorAnimationRequest &request, int palette) const;
    const ActorAnimation *objectAnimation(int identity, int mode, int palette) const;
    std::array<int, 2> objectShrineOverlays(int identity, int code) const;
    ObjectPresentation objectPresentation(int identity, int serverMode, float elapsed) const;
    std::string actorSequenceMode(std::string_view name) const;
    const Sprite *groundItemSprite(const InventoryItemView &) const;
    void drawGroundItem(const InventoryItemView &, bool highlighted,
                        const PaletteBlendView *lighting = nullptr, Color light = WHITE) const;
    void drawEnemyBar(std::string_view title, std::optional<float> life, std::string_view description = {}, Color color = WHITE) const;
    void drawMonsterCorpseName(const std::string &) const;
    void refreshUi(float dt);
    void advanceWorldPresentation(float dt);
    ~SceneView();
    ViewState &ui();
    const ViewState &ui() const;
    const InventoryView &inventoryView() const;
    InventoryDrop inventoryDropAt(Vec mouse) const;
    size_t goldDialogCaretAt(Vec mouse) const;
    void refreshInventory();
    const CharacterView &characterView() const;
    void refreshCharacterView();
    const QuestView &questView() const;
    bool refreshQuests();
    void openQuestLog();
    bool openQuestLogPreview();
    void closeQuestLogPreview();
    const NpcConversationView &npcView() const;
    const MapSceneView &mapView() const;
    const ShopView &shopView() const;
    const HirelingView &hirelingView() const;
    const HirelingListView &hirelingListView() const;
    void refreshInteractions();
    void refreshNpcView(EntityId npc);
    Vec screen(Vec position) const;
    void shakeScreen(int magnitude,int buildFrames,int holdFrames,int fadeFrames,float delay);
    Vec world(Vec position) const;
    void drawInteractionLabel(const std::string &, Vec, int offset = 70) const;
    void drawPortalLabel(const std::string &, Vec ground, int nameOffset) const;
    void drawNpcLabel(const std::string &, Vec ground, int pixelHeight) const;
    void drawInteractionDetails(const std::vector<std::string> &,Vec) const;
    void drawPlayerLabel(const std::string &, Vec ground, bool hostile, int life) const;
    void drawOverheadText(const std::string &,Vec);
    bool leftSkillAllowed(int skill) const;
    struct SkillPickerSlot { std::optional<int> skill; Rectangle bounds; uint32_t owner=UINT32_MAX; };
    std::vector<SkillPickerSlot> skillPickerSlots(bool right) const;
    std::optional<int> skillAt(Vec mouse) const;
    std::optional<ItemHandle> lootAt(Vec mouse, bool labelsOnly = false) const;
    void advanceUi(float dt);
    void pauseWorldPresentation(bool paused);
    void notice(std::string text, bool error = false);
    void openNpcDialogue(EntityId object, std::string speaker, std::string text, std::optional<uint32_t> stringId = {});
    bool npcSpeechFinished() const;
    bool canSkipNpcSpeech() const;
    void cancelNpcDialogue();
    void openNpcMenu(EntityId object, std::string speaker, bool firstIntroduction);
    bool startNpcTalk();
    bool startNpcIntroduction();
    bool startNpcTopic(QuestId quest);
    bool startNpcTextTopic(uint32_t topic);
    bool openNpcShop(bool gamble = false);
    bool npcShopRepairAllAt(Vec mouse) const;
    void closeNpcShop();
    bool npcShopDropAt(Vec mouse) const;
    std::optional<NpcMenuSelection> clickNpcMenu(Vec mouse) const; // Empty only outside the menu box.
    void scrollNpcDialogue(int amount);
    bool closeNpcDialogue();
    std::optional<uint32_t> clickNpcShop(Vec mouse, bool directBuy = false);
    int hirelingConfirmationAt(Vec mouse) const;
    bool handleNpcShopRejection(Vec mouse, bool confirm, bool click);
    std::optional<RegionId> clickWaypointMenu(Vec mouse);
    void scrollNpcShop(int pages);
    bool showNextNpcGossip();
    std::vector<TravelEntryView> travelEntries() const;
};
} // namespace d2x
