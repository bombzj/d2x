#pragma once
#include "audio.hpp"
#include "content/automap_data.hpp"
#include "gameplay/session/session.hpp"
#include "primitives.hpp"
#include <set>

namespace d2x {
// GPU and audio handles belong to the view, never to saveable game state.
class SceneAssets {
    Graphics graphics_;
    Graphics uiGraphics_;
    Graphics unitsGraphics_;
    AutomapCatalog automapCatalog_;
    void loadProps(const Region &region);
    void loadMonsterAnimations(Archives &archives, const GameSession &session);
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
    int frostNovaMissileId = -1;
    float frostNovaVelocity = 0;
    std::map<std::string, GpuAnimation> skillTrees;
    Sprite attackIcon;
    std::vector<std::vector<Sprite>> regionTiles;
    std::vector<std::vector<AutomapStamp>> regionAutomap;
    // 0: original maximaps.dc6, 1: original maximap.dc6.
    std::array<std::map<int, Sprite>, 2> automapCels;
    std::map<std::string, GpuAnimation> propAnimations, npcWalkAnimations, hero;
    std::map<std::string, GpuAnimation> hirelingAnimations;
    std::map<std::string, std::array<GpuAnimation, 3>> waypointAnimations;
    std::map<std::string, std::array<GpuAnimation, 3>> objectModeAnimations;
    std::map<MonsterKind, std::map<std::string, GpuAnimation>> monsterAnimations;
    std::map<std::string, std::map<std::string, GpuAnimation>, std::less<>> monsterVariantAnimations;
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
    GpuAnimation fireball, fireburst, teleportOverlay, panel, miniPanel, miniPanelButtons, miniPanelToggle,
        cursor, inventoryPanel, attributeButtons,
        attributePoints, weaponTabs, vendorPanel, vendorTabs,
        questBackground, questSockets, questTabs, questClose, questReplay, goldCoin,
        vendorButtons, vendorConfirm, waypointBorder, waypointPanel, waypointTabs, waypointIcons,
        storagePanel, cubePanel, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    std::array<GpuAnimation, 6> actOneQuestIcons;
    GpuAnimation hirelingPanel, hirelingScroll, hirelingHead, hirelingArmor, hirelingWeapon;
    struct OverlayArt {
        GpuAnimation animation;
        Vec offset;
        std::array<int, 4> heights{};
        int frames = 0, fps = 0, trans = 5;
    } npcAlert;
    SceneAssets(Archives &archives, const GameSession &session);
    static std::string itemArtKey(const ItemInstance &item);
    void loadInventoryArt(const GameSession &session);
    void loadHeroEquipment(const GameSession &session);
    const std::string &heroAppearanceError() const { return heroFailure_; }
    int automapObjectCel(int objectClass) const { return automapCatalog_.objectCel(objectClass); }
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters);
    void loadMonsterAudio(Archives &archives, const MonsterCatalog &monsters);
};
} // namespace d2x
