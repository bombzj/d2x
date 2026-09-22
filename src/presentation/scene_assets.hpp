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
    std::map<std::string, GpuAnimation> propAnimations, hero, fallen, zombie;
    std::map<std::string, GpuAnimation> itemGround, itemIcons;
    GpuAnimation fireball, fireburst, panel, cursor, inventoryPanel, storagePanel, beltPanel, beltSocket,
        orbs, globeOverlap, runButton, button;
    SceneAssets(Archives &archives, const GameSession &session);
    void collectMapVariants(Archives &archives, const WorldCatalog &catalog, const MonsterCatalog &monsters);
};
} // namespace d2x
