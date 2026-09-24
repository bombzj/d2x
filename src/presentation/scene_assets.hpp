#pragma once
#include "audio.hpp"
#include "gameplay/session/session.hpp"
#include "primitives.hpp"

namespace d2x {
// GPU and audio handles belong to the view, never to saveable game state.
class SceneAssets {
    Graphics graphics_;
    Graphics uiGraphics_;
    void loadProps(const Region &region);
    void loadMonsterAnimations(Archives &archives, const GameSession &session);
    void loadSkillIcons(Archives &archives, const ClassicData &content);
    std::string heroKey_;
    std::map<std::string, std::map<std::string, GpuAnimation>> heroCache_;
    std::map<std::string, std::string> heroErrors_;
    std::string heroFailure_;

  public:
    SoundBank audio;
    ClassicFont font, speechFont;
    struct SkillIcon {
        Sprite sprite;
        bool leftAllowed = false;
    };
    std::map<int, SkillIcon> skillIcons;
    std::map<int, GpuAnimation> projectileAnimations;
    int frostNovaMissileId = -1;
    float frostNovaVelocity = 0;
    std::map<std::string, GpuAnimation> skillTrees;
    Sprite attackIcon;
    std::vector<std::vector<Sprite>> regionTiles;
    std::map<std::string, GpuAnimation> propAnimations, npcWalkAnimations, hero;
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
    GpuAnimation fireball, fireburst, teleportOverlay, panel, cursor, inventoryPanel, attributeButtons,
        attributePoints, vendorPanel, vendorTabs,
        vendorButtons, vendorConfirm, waypointBorder, waypointPanel, waypointTabs, waypointIcons,
        storagePanel, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    SceneAssets(Archives &archives, const GameSession &session);
    static std::string itemArtKey(const ItemInstance &item);
    void loadInventoryArt(const GameSession &session);
    void loadHeroEquipment(const GameSession &session);
    const std::string &heroAppearanceError() const { return heroFailure_; }
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters);
    void loadMonsterAudio(Archives &archives, const MonsterCatalog &monsters);
};
} // namespace d2x
