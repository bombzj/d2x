#pragma once
#include "input.hpp"
#include "inventory_panel.hpp"
#include "scene_assets.hpp"

namespace d2x {
struct ViewState {
    InventoryUi inventory;
    std::array<Skill, hotbarSlots> hotbar{Skill::Fireball, Skill::FrostNova, Skill::Whirlwind,
                                          Skill::Teleport, Skill::Leap,      Skill::WarCry};
    Vec camera, clickAt;
    float clickAge = 10, zoom = 1;
    bool help = false, automap = false, debug = false, pause = false, travelMenu = false;
    bool showLoot = false;
    int selected = 0, travelPage = 0;
    float animationTime = 0, heroTime = 0, stepClock = 0;
    std::string heroMode = "nu", dialogue;
    std::string lootNotice;
    float noticeTime = 0;
    bool noticeError = false;
    bool blocksWorld() const { return pause || travelMenu || help; }
};
class SceneView {
    const GameSession &session_;
    SceneAssets assets_;
    UiPainter painter_;
    ViewState view_;
    std::map<EntityId, float> landingAge_;
    struct LootLabel {
        ItemHandle item;
        std::string text;
        Rectangle bounds;
        Vec ground;
        Color color;
    };
    std::vector<LootLabel> lootLabels(Vec mouse) const;
    Rectangle lootBounds(const ItemInstance &item) const;
    void drawGroundItem(EntityId item) const;
    void drawLootLabels(Vec mouse) const;
    void drawTerrain() const;
    void drawActors() const;
    void drawMagic() const;
    void drawMinimap(bool large) const;
    void drawHud() const;
    void drawHelp() const;
    void drawInventory(Vec mouse) const;
    void drawStorage(Vec mouse) const;
    void drawContainerGrid(const ContainerGrid &grid, Vec mouse) const;
    void drawBelt(Vec mouse) const;
    void drawItemTooltip(const ItemInstance &item, Vec anchor) const;
    void itemButton(Rectangle bounds, const char *label, Color color) const;
    void drawItemIcon(const ItemInstance &item, Rectangle bounds, Color tint = WHITE) const;
    void drawInventoryCursor(Vec mouse) const;
    void orb(int x, int y, float value, Color light, Color dark, const std::string &text) const;

  public:
    SceneView(Archives &archives, const GameSession &session);
    ViewState &ui() { return view_; }
    const ViewState &ui() const { return view_; }
    Vec screen(Vec position) const;
    Vec world(Vec position) const;
    bool visible(const WorldObject &object) const;
    std::optional<ItemHandle> lootAt(Vec mouse, bool labelsOnly = false) const;
    void toggleMute() { assets_.audio.muted = !assets_.audio.muted; }
    void advance(float dt);
    void draw(Vec mouse) const;
    void notice(std::string text, bool error = false);
    void sessionRestored();
    void collectMapVariants(Archives &archives) {
        assets_.collectMapVariants(archives, session_.worldContent(), session_.monsterContent());
    }
};
std::string playerAnimationMode(const PlayerState &player);
} // namespace d2x
