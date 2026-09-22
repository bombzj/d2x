#include "hud_layout.hpp"
#include "scene_view.hpp"
#include <algorithm>

namespace d2x {
namespace {
void imageAt(const Sprite *image, Rectangle bounds, Color tint = WHITE) {
    if (!image || !image->texture.id)
        return;
    const auto &texture = image->texture;
    DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds, {0, 0}, 0, tint);
}
} // namespace
void SceneView::orb(bool mana, float fraction) const {
    const auto *image = assets_.orbs.frame(0, mana ? 1 : 0);
    if (!image)
        return;
    auto bounds = hudGlobe(mana);
    const auto &texture = image->texture;
    int filled = int(std::round(std::clamp(fraction, 0.f, 1.f) * texture.height));
    if (filled > 0) {
        float empty = float(texture.height - filled);
        auto destination = bounds;
        destination.y += empty * hudScale;
        destination.height = filled * hudScale;
        DrawTexturePro(texture, {0, empty, float(texture.width), float(filled)}, destination, {0, 0}, 0,
                       WHITE);
    }
    // Original foreground rim/fingers, not the opaque empty-globe panel backing.
    imageAt(assets_.globeOverlap.frame(0, mana ? 1 : 0), hudRect(mana ? 691 : 28, mana ? 96 : 93, 82, 88));
}
void SceneView::drawSkillIcon(std::optional<Skill> skill, Rectangle bounds) const {
    const auto &player = session_.state().player;
    const auto *image = skill ? &assets_.skillIcons.at(size_t(*skill)).sprite : &assets_.attackIcon;
    bool available = !player.dead && (!skill || player.mana >= skillDefinition(*skill).manaCost);
    imageAt(image, bounds, available ? WHITE : Color{95, 95, 95, 255});
    if (skill && player.cooldown[size_t(*skill)] > 0) {
        auto time = std::string(TextFormat("%.1f", player.cooldown[size_t(*skill)]));
        DrawRectangleRec(bounds, {0, 0, 0, 115});
        painter_.label(time, int(bounds.x + (bounds.width - painter_.measure(time, 12)) / 2),
                       int(bounds.y + bounds.height / 2 - 6), 12, parchment);
    }
}
void SceneView::drawControlPanel() const {
    const auto &player = session_.state().player;
    DrawRectangle(0, H - HUD, W, HUD, BLACK);
    // Use the original globe silhouette to mask the world above the panel.
    // The panel then supplies the original empty bowl and glass highlights.
    for (bool mana : {false, true})
        imageAt(assets_.orbs.frame(0, mana ? 1 : 0), hudGlobe(mana), BLACK);
    constexpr std::array<float, 6> offsets{0, 165, 293, 421, 549, 683};
    for (int i = 0; i < int(offsets.size()); ++i) {
        const auto &part = assets_.panel.frames.at(i);
        imageAt(&part, hudRect(offsets[i], float(part.texture.height), float(part.texture.width),
                               float(part.texture.height)));
    }
    orb(false, player.hp / playerRules().maxLife);
    orb(true, player.mana / playerRules().maxMana);
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false));
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true));
    auto stamina = hudStamina();
    stamina.width *= std::clamp(player.stamina / playerRules().maxStamina, 0.f, 1.f);
    DrawRectangleRec(stamina, {170, 136, 68, 175});
    imageAt(assets_.runButton.frame(0, player.running ? 2 : 0), hudRunButton());
    // Experience and unspent attribute/skill points are not implemented. Their
    // original panel sockets remain empty instead of displaying invented progress.
}
std::vector<std::optional<Skill>> SceneView::skillChoices(bool right) const {
    std::vector<std::optional<Skill>> choices{std::nullopt}; // ordinary attack
    for (auto skill : view_.hotbar)
        if (right || leftSkillAllowed(skill))
            choices.push_back(skill);
    return choices;
}
void SceneView::drawSkillControls(Vec mouse) const {
    if (view_.blocksWorld() || view_.inventory.open || view_.inventory.drag)
        return;
    std::optional<std::optional<Skill>> hovered;
    if (view_.skillPicker) {
        bool right = *view_.skillPicker;
        auto choices = skillChoices(right);
        for (size_t i = 0; i < choices.size(); ++i) {
            auto bounds = hudPickerSlot(right, int(i));
            drawSkillIcon(choices[i], bounds);
            if (choices[i]) {
                auto slot = std::find(view_.hotbar.begin(), view_.hotbar.end(), *choices[i]);
                auto label = "F" + std::to_string(5 + int(slot - view_.hotbar.begin()));
                painter_.label(label, int(bounds.x + 3), int(bounds.y + bounds.height - 12), 10, gold);
            }
            if (CheckCollisionPointRec(rv(mouse), bounds)) {
                hovered.emplace(choices[i]);
                DrawRectangleLinesEx(bounds, 1, gold);
            }
        }
    } else {
        for (bool right : {false, true})
            if (CheckCollisionPointRec(rv(mouse), hudSkillSlot(right)))
                hovered.emplace(right ? view_.rightSkill : view_.leftSkill);
    }
    if (hovered) {
        auto choice = *hovered;
        auto name = choice ? skillDefinition(*choice).name : "Attack";
        auto detail = choice ? std::string(skillDefinition(*choice).description) : "Normal weapon attack";
        if (choice)
            detail += "  /  " + std::to_string(int(skillDefinition(*choice).manaCost)) + " mana";
        float y = H - (view_.skillPicker ? 108 : 55) * hudScale - 60;
        DrawRectangle(W / 2 - 260, int(y), 520, 52, {0, 0, 0, 225});
        painter_.centered(name, int(y + 7), 16, gold);
        painter_.centered(detail, int(y + 29), 12);
    }
    for (bool mana : {false, true}) {
        if (!CheckCollisionPointRec(rv(mouse), hudGlobe(mana)))
            continue;
        const auto &p = session_.state().player;
        auto text = std::string(mana ? "Mana: " : "Life: ") + std::to_string(int(mana ? p.mana : p.hp)) +
                    " / " + std::to_string(int(mana ? playerRules().maxMana : playerRules().maxLife));
        auto globe = hudGlobe(mana);
        painter_.label(text, int(globe.x + (globe.width - painter_.measure(text, 12)) / 2), int(globe.y - 20),
                       12, parchment);
    }
    std::string hint;
    if (CheckCollisionPointRec(rv(mouse), hudRunButton()))
        hint = "Walk / Run [SPACE]";
    if (CheckCollisionPointRec(rv(mouse), hudMenuButton()))
        hint = "Backpack [I]";
    if (CheckCollisionPointRec(rv(mouse), hudStamina()))
        hint = "Stamina: " + std::to_string(int(session_.state().player.stamina)) + " / 100";
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
