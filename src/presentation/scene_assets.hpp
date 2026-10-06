#pragma once
#include "presentation/audio/audio.hpp"
#include "content/world/automap_data.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/skills/visual.hpp"
#include "gameplay/quest/id.hpp"
#include "presentation/world/missile_visual.hpp"
#include "gameplay/monsters/kind.hpp"
#include "client/map_asset_source.hpp"
#include "world/object_animation.hpp"
#include "world/object.hpp"
#include "presentation/graphics/primitives.hpp"
#include <set>
#include <memory>
#include <tuple>

namespace d2x {
class GameSession;
class MonsterCatalog;
class WorldCatalog;
struct LevelRecord;
struct Map;
struct MonsterRecord;
struct Region;
struct WorldObject;
struct ClassicData;
struct InventoryView;
struct MonsterIdentity;
struct MonsterEnchantment;
// GPU and audio handles belong to the view, never to saveable game state.
class SceneAssets {
    Archives &archives_;
    Table objectDefinitions_;
    std::unique_ptr<DataTable> missileDefinitions_;
    std::unique_ptr<DataTable> combatSounds_;
    std::map<std::string, size_t, std::less<>> combatSoundRows_;
    std::map<int, size_t> missileRows_;
    void loadProjectileDefinitions(const ClassicData &);
    mutable std::map<std::pair<int, int>, WorldObject> decorations_;
    // Mutable so the const draw path can populate the on-demand caches below.
    mutable Graphics graphics_;
    mutable std::array<std::unique_ptr<Graphics>, 5> actGraphics_;
    Graphics &graphicsForAct(int act) const;
    Graphics uiGraphics_;
    Graphics unitsGraphics_;
    AutomapCatalog automapCatalog_;
    std::unique_ptr<WorldCatalog> worldDefinitions_;
    mutable std::vector<const Tile *> worldTileSources_;
    mutable std::vector<Sprite> worldTiles_;
    mutable int worldTilePalette_ = -1;
    void loadWorldLightDefinitions();
    // One entry per implemented monster art set. Built without touching the MPQ so
    // the first sighting of a class uploads its own frames instead of the whole act.
    struct MonsterArtSource {
        MonsterKind kind = MonsterKind::Fallen;
        const MonsterRecord *actor = nullptr;
        size_t shield = 0;
        bool base = false;
        int paletteOverride = -1;
        bool fixedPalette = false;
    };
    std::map<std::string, MonsterArtSource, std::less<>> monsterArtSources;
    std::map<MonsterKind, MonsterArtSource> baseMonsterArt;
    // Filled on first sighting; mutable so the const draw path can populate them.
    mutable std::map<MonsterKind, std::map<std::string, GpuAnimation>> monsterAnimations;
    mutable std::map<std::string, std::map<std::string, GpuAnimation>, std::less<>>
        monsterVariantAnimations;
    mutable std::map<std::string, std::vector<int>, std::less<>> elitePaletteChoices_;
    void loadProps(const Region &region);
    void indexMonsterArt(const GameSession &session);
    void loadMonsterActor(const GameSession &session, const MonsterArtSource &source,
                          std::map<std::string, GpuAnimation> &animations) const;
    void loadHirelingAnimations(Archives &archives, const GameSession &session);
    void loadUi(Archives &, const ClassicData &, bool multiplayer);
    void loadNpcAlert(const DataTable &);
    void loadIceShatter();
    void loadSkillIcons(Archives &archives, const ClassicData &content);
    void loadAutomap(const IMapAssetSource &source);
    std::string heroKey_;
    std::map<std::string, std::map<std::string, GpuAnimation>> heroCache_;
    std::map<std::string, std::string> heroErrors_;
    std::string heroFailure_;

  public:
    ~SceneAssets();
    SoundBank audio;
    ClassicFont font, speechFont;
    struct SkillIcon {
        Sprite sprite;
        bool leftAllowed = false;
    };
    std::map<int, SkillIcon> skillIcons;
    std::map<int, GpuAnimation> projectileAnimations;
    std::set<int> unavailableProjectiles, unavailableOverlays;
    std::set<int> translucentProjectiles;
    std::set<int> frozenOrbProjectiles;
    std::map<int, BlizzardVisual> blizzardFalls;
    std::map<int, MeteorVisual> meteorVisuals;
    struct ObjectLight {
        std::array<int, 8> diameter{};
        Color color{0, 0, 0, 255};
        bool flicker = false; // Native modulation program is not yet recovered.
    };
    std::map<int, ObjectLight> objectLights;
    struct OverlayLight {
        int initialRadius = 0, radius = 0;
        Color color{0, 0, 0, 255};
    };
    std::map<int, OverlayLight> overlayLights;
    std::map<std::string, int, std::less<>> overlayIds;
    struct ProjectileVisual {
        float fps = 25;
        bool loop = false;
        int frames = 0;
        int loopStart = 0, loopEnd = 0;
        float lifetime = 0;
        int initSteps = 0;
        int trans = 0;
        int lightRadius = 0;
        Color lightColor{0, 0, 0, 255};
        bool lightFlicker = false;
    };
    std::map<int, ProjectileVisual> projectileVisuals;
    std::map<int, ClientMissileProgram> clientMissilePrograms;
    std::map<std::string, int, std::less<>> weaponMissiles;
    // Client-only impact alternatives (CltHit03); never damage-bearing missiles.
    std::map<int, std::array<int, 2>> projectileImpactVariants;
    std::map<int, int> projectileFreezingEjecta;
    std::array<int, 3> iceShatterProjectiles{-1, -1, -1};
    std::map<int, int> iceShatterMelts;
    struct SpellOverlay {
        GpuAnimation animation;
        SkillOverlayVisual visual;
    };
    std::map<int, SpellOverlay> spellOverlays;
    std::map<std::string, GpuAnimation> skillTrees;
    Sprite attackIcon;
    // Region tiles and prop art are uploaded the first time their region is drawn,
    // so a session no longer pays for every level in the act up front.
    std::vector<std::vector<const Tile *>> regionTileSources;
    std::vector<bool> regionAutomapLoaded;
    void syncRegions(const IMapAssetSource &source);
    std::set<std::string, std::less<>> propArtKeys;
    mutable bool propArtReported = false;
    void indexPropArt(const IMapAssetSource &source);
    void loadPropObject(const WorldObject &object) const;
    std::vector<std::vector<AutomapStamp>> regionAutomap;
    uint64_t automapContentFingerprint = 0;
    // 0: original maximaps.dc6, 1: original maximap.dc6.
    mutable std::array<std::map<int, Sprite>, 2> automapCels;
    mutable std::map<std::tuple<int, int, bool>, std::vector<Sprite>> townAutomapArt;
    std::vector<std::array<std::vector<Sprite>, 2>> regionTownAutomap;
    std::vector<int> regionAutomapVariants;
    struct MonsterLight { int radius = 0; Color color{0, 0, 0, 255}; };
    std::map<int, MonsterLight> monsterLights;
    // Filled on first sighting; mutable so the const draw path can populate them.
    mutable std::map<std::string, GpuAnimation> propAnimations, npcWalkAnimations, hero;
    std::map<std::string, GpuAnimation> hirelingAnimations;
    std::map<int, GpuAnimation> hirelingPortraits;
    std::map<int, GpuAnimation> summonPortraits;
    mutable std::map<std::string, std::array<GpuAnimation, 3>> waypointAnimations;
    mutable std::map<std::string, std::array<GpuAnimation, 8>> objectModeAnimations;
    struct MonsterAudio {
        std::string attack1, attack2, skill1, skill2, hit, death, footstep, neutral;
        float footstepInterval = 0, neutralInterval = 0;
    };
    std::map<std::string, MonsterAudio, std::less<>> monsterAudio;
    std::map<std::string, GpuAnimation> itemGround, itemIcons;
    std::array<GpuAnimation, 2> townPortalAnimations;
    std::array<ObjectAnimationRule, 2> townPortalRules;
    std::array<GpuAnimation, 2> cainPortalAnimations;
    std::array<ObjectAnimationRule, 2> cainPortalRules;
    GpuAnimation panel, miniPanel, miniPanelButtons, miniPanelToggle,
        cursor, targetingCursors, inventoryPanel, attributeButtons,
        attributePoints, weaponTabs, vendorPanel, vendorTabs,
        questBackground, questSockets, questTabs, questClose, questReplay, goldCoin,
        vendorButtons, vendorConfirm, waypointBorder, waypointPanel, waypointTabs, waypointIcons,
        storagePanel, cubePanel, orificePanel, orificeButtons, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    std::string waypointTitle;
    std::array<ClassicFont, 3> waypointFonts; // Native PL2 white, blue and dark grey.
    std::array<GpuAnimation, size_t(QuestId::Count)> questIcons;
    std::array<Rectangle, size_t(QuestId::Count)> questFaces{};
    std::array<GpuAnimation, 7> tombSymbols;
    std::array<GpuAnimation, 3> gameMenuLabels;
    std::array<GpuAnimation, 5> optionsMenuLabels;
    std::array<GpuAnimation, 5> automapOptionLabels;
    GpuAnimation automapOptionsTitle;
    std::array<GpuAnimation, 10> automapOptionValues;
    GpuAnimation gameMenuMarker;
    GpuAnimation hirelingPanel, hirelingScroll, hirelingHead, hirelingArmor, hirelingWeapon;
    struct OverlayArt {
        GpuAnimation animation;
        Vec offset;
        std::array<int, 4> heights{};
        int frames = 0, trans = 5;
        float fps = 0;
        bool preDraw = false;
    } npcAlert;
    std::map<int, std::array<OverlayArt, 2>> shrineOverlays, combatStateOverlays;
    SceneAssets(Archives &archives, const ClassicData &content);
    const ProjectileVisual *ensureProjectile(int id);
    const SkillOverlayVisual *ensureOverlay(int id);
    bool playOriginalCombatSound(std::string_view name, uint64_t frame);
    void loadInventoryArt(const InventoryView &inventory, int palette);
    SceneAssets(Archives &archives, const GameSession &session, const IMapAssetSource &source);
    static std::string itemArtKey(const ItemInstance &item);
    // Returns the frame set for a live unit, building and caching it on first use.
    // Unimplemented classes keep the authorized substitute set, as at load time.
    const std::map<std::string, GpuAnimation> &monsterAnimationSet(const GameSession &session,
                                                                   std::string_view monsterClass,
                                                                   MonsterKind kind,
                                                                   int summonShield,
                                                                   const MonsterIdentity *identity = nullptr,
                                                                   const MonsterEnchantment *enchantment = nullptr) const;
    // Region terrain uploads on first draw, and prop art on first sighting.
    void ensurePropArt(const WorldObject &object) const;
    const WorldObject &clientDecoration(int id, int palette) const;
    // True when the region tables promise art for this key; the draw path builds it.
    bool propArtAvailable(std::string_view key) const {
        return propArtKeys.contains(key);
    }
    void loadInventoryArt(const GameSession &session);
    void loadHeroEquipment(const GameSession &session);
    const std::string &heroAppearanceError() const { return heroFailure_; }
    int automapObjectCel(int objectClass) const { return automapCatalog_.objectCel(objectClass); }
    const Sprite *automapSprite(int cel, bool large) const;
    const std::vector<Sprite> &townAutomapSprites(int level, int variant, bool large) const;
    int automapNpcCel(std::string_view monsterClass) const { return automapCatalog_.npcCel(monsterClass); }
    const LevelRecord &worldLevel(int id) const;
    const std::vector<Sprite> &worldTileSprites(const Map &, int palette) const;
    void resetWorldTiles() { worldTileSources_.clear(); worldTiles_.clear(); worldTilePalette_ = -1; }
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters, uint32_t mapSeed, uint32_t objectSeed);
    void loadMonsterAudio(Archives &archives, const MonsterCatalog &monsters);
};
} // namespace d2x
