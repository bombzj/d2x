#pragma once
#include "audio.hpp"
#include "content/automap_data.hpp"
#include "gameplay/session/session.hpp"
#include "primitives.hpp"
#include <set>

namespace d2x {
// GPU and audio handles belong to the view, never to saveable game state.
class SceneAssets {
    Archives &archives_;
    // Mutable so the const draw path can populate the on-demand caches below.
    mutable Graphics graphics_;
    Graphics uiGraphics_;
    Graphics unitsGraphics_;
    AutomapCatalog automapCatalog_;
    // One entry per implemented monster art set. Built without touching the MPQ so
    // the first sighting of a class uploads its own frames instead of the whole act.
    struct MonsterArtSource {
        MonsterKind kind = MonsterKind::Fallen;
        const MonsterRecord *actor = nullptr;
        size_t shield = 0;
        bool base = false;
    };
    std::map<std::string, MonsterArtSource, std::less<>> monsterArtSources;
    std::map<MonsterKind, MonsterArtSource> baseMonsterArt;
    // Filled on first sighting; mutable so the const draw path can populate them.
    mutable std::map<MonsterKind, std::map<std::string, GpuAnimation>> monsterAnimations;
    mutable std::map<std::string, std::map<std::string, GpuAnimation>, std::less<>>
        monsterVariantAnimations;
    void loadProps(const Region &region);
    void indexMonsterArt(const GameSession &session);
    void loadMonsterActor(const GameSession &session, const MonsterArtSource &source,
                          std::map<std::string, GpuAnimation> &animations) const;
    void loadHirelingAnimations(Archives &archives, const GameSession &session);
    void loadSkillIcons(Archives &archives, const ClassicData &content);
    void loadAutomap(const GameSession &session);
    std::string heroKey_;
    std::map<std::string, std::map<std::string, GpuAnimation>> heroCache_;
    std::map<std::string, std::string> heroErrors_;
    std::string heroFailure_;

  public:
    struct AutomapStamp {
        int x = 0, y = 0, cel = -1;
    };
    SoundBank audio;
    ClassicFont font, speechFont;
    struct SkillIcon {
        Sprite sprite;
        bool leftAllowed = false;
    };
    std::map<int, SkillIcon> skillIcons;
    std::map<int, GpuAnimation> projectileAnimations;
    std::set<int> translucentProjectiles;
    struct ProjectileVisual {
        float fps = 25;
        bool loop = false;
        int frames = 0;
        int loopStart = 0, loopEnd = 0;
        float lifetime = 0;
    };
    std::map<int, ProjectileVisual> projectileVisuals;
    // Client-only impact alternatives (CltHit03); never damage-bearing missiles.
    std::map<int, std::array<int, 2>> projectileImpactVariants;
    struct SpellOverlay {
        GpuAnimation animation;
        SkillSpec::OverlayVisual visual;
    };
    std::map<int, SpellOverlay> spellOverlays;
    std::map<std::string, GpuAnimation> skillTrees;
    Sprite attackIcon;
    // Region tiles and prop art are uploaded the first time their region is drawn,
    // so a session no longer pays for every level in the act up front.
    mutable std::vector<std::vector<Sprite>> regionTiles;
    std::vector<std::vector<const Tile *>> regionTileSources;
    mutable std::vector<bool> regionTilesUploaded;
    std::set<std::string, std::less<>> propArtKeys;
    mutable bool propArtReported = false;
    void indexPropArt(const GameSession &session);
    void loadPropObject(const WorldObject &object) const;
    std::vector<std::vector<AutomapStamp>> regionAutomap;
    // 0: original maximaps.dc6, 1: original maximap.dc6.
    std::array<std::map<int, Sprite>, 2> automapCels;
    // Filled on first sighting; mutable so the const draw path can populate them.
    mutable std::map<std::string, GpuAnimation> propAnimations, npcWalkAnimations, hero;
    std::map<std::string, GpuAnimation> hirelingAnimations;
    std::map<int, GpuAnimation> summonPortraits;
    mutable std::map<std::string, std::array<GpuAnimation, 3>> waypointAnimations;
    mutable std::map<std::string, std::array<GpuAnimation, 3>> objectModeAnimations;
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
        storagePanel, cubePanel, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    std::array<GpuAnimation, 6> actOneQuestIcons;
    std::array<Rectangle, 6> actOneQuestFaces{};
    std::array<GpuAnimation, 3> gameMenuLabels;
    GpuAnimation gameMenuMarker;
    GpuAnimation hirelingPanel, hirelingScroll, hirelingHead, hirelingArmor, hirelingWeapon, hirelingPortrait;
    struct OverlayArt {
        GpuAnimation animation;
        Vec offset;
        std::array<int, 4> heights{};
        int frames = 0, trans = 5;
        float fps = 0;
    } npcAlert;
    std::map<int, std::array<OverlayArt, 2>> shrineOverlays, combatStateOverlays;
    SceneAssets(Archives &archives, const GameSession &session);
    static std::string itemArtKey(const ItemInstance &item);
    // Returns the frame set for a live unit, building and caching it on first use.
    // Unimplemented classes keep the authorized substitute set, as at load time.
    const std::map<std::string, GpuAnimation> &monsterAnimationSet(const GameSession &session,
                                                                   std::string_view monsterClass,
                                                                   MonsterKind kind,
                                                                   int summonShield) const;
    // Region terrain uploads on first draw, and prop art on first sighting.
    const std::vector<Sprite> &regionTileSprites(size_t index) const;
    void ensurePropArt(const WorldObject &object) const;
    // True when the region tables promise art for this key; the draw path builds it.
    bool propArtAvailable(std::string_view key) const {
        return propArtKeys.contains(key);
    }
    void loadInventoryArt(const GameSession &session);
    void loadHeroEquipment(const GameSession &session);
    const std::string &heroAppearanceError() const { return heroFailure_; }
    int automapObjectCel(int objectClass) const { return automapCatalog_.objectCel(objectClass); }
    int automapNpcCel(std::string_view monsterClass) const { return automapCatalog_.npcCel(monsterClass); }
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters, uint32_t mapSeed, uint32_t objectSeed);
    void loadMonsterAudio(Archives &archives, const MonsterCatalog &monsters);
};
} // namespace d2x
