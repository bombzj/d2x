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
    void loadSkillIcons(Archives &archives, const ClassicData &content);
    std::string heroKey_;
    std::map<std::string, std::map<std::string, GpuAnimation>> heroCache_;
    std::map<std::string, std::string> heroErrors_;
    std::string heroFailure_;

  public:
    SoundBank audio;
    ClassicFont font;
    struct SkillIcon {
        Sprite sprite;
        bool leftAllowed = false;
    };
    std::array<SkillIcon, skillCount> skillIcons;
    Sprite attackIcon;
    std::vector<std::vector<Sprite>> regionTiles;
    std::map<std::string, GpuAnimation> propAnimations, hero;
    std::map<std::string, std::array<GpuAnimation, 3>> waypointAnimations;
    std::map<MonsterKind, std::map<std::string, GpuAnimation>> monsterAnimations;
    std::map<std::string, GpuAnimation> itemGround, itemIcons;
    std::array<GpuAnimation, 2> townPortalAnimations;
    std::array<ObjectAnimationRule, 2> townPortalRules;
    GpuAnimation fireball, fireburst, panel, cursor, inventoryPanel, storagePanel, beltPanel, beltSocket, orbs,
        globeOverlap, runButton, button;
    SceneAssets(Archives &archives, const GameSession &session);
    static std::string itemArtKey(const ItemInstance &item);
    void loadInventoryArt(const GameSession &session);
    void loadHeroEquipment(const GameSession &session);
    const std::string &heroAppearanceError() const { return heroFailure_; }
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters);
};
} // namespace d2x
