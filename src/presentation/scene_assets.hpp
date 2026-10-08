#pragma once
#include "presentation/audio/scene_audio.hpp"
#include "presentation/actors/actor_animation.hpp"
#include "content/world/automap_data.hpp"
#include "gameplay/items/state.hpp"
#include "gameplay/skills/visual.hpp"
#include "gameplay/quest/id.hpp"
#include "presentation/world/missile_visual.hpp"
#include "presentation/graphics/primitives.hpp"
#include <set>
#include <memory>
#include <tuple>

namespace d2x {
class MonsterCatalog;
class WorldCatalog;
struct LevelRecord;
struct Map;
struct ClassicData;
struct InventoryView;
// GPU and audio handles belong to the view, never to saveable game state.
class SceneAssets {
    Archives &archives_;
    Table objectDefinitions_;
    std::unique_ptr<DataTable> missileDefinitions_;
    SoundCatalog soundCatalog_;
    std::map<int, size_t> missileRows_;
    void loadProjectileDefinitions(const ClassicData &);
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
    mutable std::unique_ptr<ActorAnimationCatalog> actorAnimations_;
    std::set<int> shrineObjects_;
    std::map<int, std::array<int, 2>> shrineOverlayIds_;
    void loadUi(Archives &, const ClassicData &);
    void loadNpcAlert(const DataTable &);
    void loadIceShatter();
    void loadSkillIcons(Archives &archives, const ClassicData &content);

  public:
    ~SceneAssets();
    SoundBank audio;
    SceneAudio sceneAudio;
    ClassicFont font, speechFont, skillGreenFont, characterLabelFont, characterPointFont,
        characterCompactFont, characterRedFont, skillLevelBlueFont, skillLevelCompactFont,
        skillLevelCompactBlueFont, skillLevelCompactRedFont;
    std::map<std::string, std::string, std::less<>> characterLabels;
    std::array<std::string, 2> globeTextFormats;
    struct SkillIcon {
        Sprite sprite;
        bool leftAllowed = false;
        Sprite treeDisabled, treeHovered;
    };
    std::map<int, SkillIcon> skillIcons;
    std::map<int, GpuAnimation> projectileAnimations;
    std::set<int> unavailableProjectiles, unavailableOverlays, imageLessProjectiles;
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
    // Legacy map metadata; GPU terrain and actor art use the shared caches.
    // 0: original maximaps.dc6, 1: original maximap.dc6.
    mutable std::array<std::map<int, Sprite>, 2> automapCels;
    mutable std::map<std::tuple<int, int, bool>, std::vector<Sprite>> townAutomapArt;
    struct MonsterLight { int radius = 0; Color color{0, 0, 0, 255}; };
    std::map<int, MonsterLight> monsterLights;
    std::map<int, GpuAnimation> hirelingPortraits, summonPortraits;
    std::map<std::string, GpuAnimation> itemGround, itemIcons;
    GpuAnimation panel, miniPanel, miniPanelButtons, miniPanelToggle,
        cursor, targetingCursors, inventoryPanel, attributeButtons,
        attributePoints, attributeSocket, weaponTabs, vendorPanel, vendorTabs,
        questBackground, questSockets, questTabs, questClose, questReplay, goldCoin,
        vendorButtons, vendorConfirm, waypointBorder, waypointPanel, waypointTabs, waypointIcons,
        storagePanel, cubePanel, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    std::string waypointTitle;
    std::string questNoticeLabel;
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
    SceneAssets(Archives &archives, const ClassicData &content);
    const ProjectileVisual *ensureProjectile(int id);
    const SkillOverlayVisual *ensureOverlay(int id);
    void loadInventoryArt(const InventoryView &inventory, int palette);
    const ActorAnimation *actorAnimation(const ActorAnimationRequest &, int palette) const;
    const ActorAnimation *objectAnimation(int identity, int mode, int palette) const;
    std::array<int, 2> objectShrineOverlays(int identity, int code) const;
    ObjectPresentation objectPresentation(int identity, int serverMode, float elapsed) const;
    std::string actorSequenceMode(std::string_view name) const;
    int automapObjectCel(int objectClass) const { return automapCatalog_.objectCel(objectClass); }
    const Sprite *automapSprite(int cel, bool large) const;
    const std::vector<Sprite> &townAutomapSprites(int level, int variant, bool large) const;
    int automapNpcCel(std::string_view monsterClass) const { return automapCatalog_.npcCel(monsterClass); }
    const LevelRecord &worldLevel(int id) const;
    const std::vector<Sprite> &worldTileSprites(const Map &, int palette) const;
    void resetWorldTiles() { worldTileSources_.clear(); worldTiles_.clear(); worldTilePalette_ = -1; }
};
} // namespace d2x
