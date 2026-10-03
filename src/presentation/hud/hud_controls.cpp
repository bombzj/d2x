#include "hud_layout.hpp"
#include "presentation/scene_view.hpp"
#include <algorithm>
#include <sstream>

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
void SceneView::drawSkillIcon(std::optional<int> skill, Rectangle bounds) const {
    const auto *entry = skill ? characterView_.skill(*skill) : nullptr;
    auto icon = skill ? assets_.skillIcons.find(*skill) : assets_.skillIcons.end();
    const auto *image = !skill ? &assets_.attackIcon
                              : icon != assets_.skillIcons.end() ? &icon->second.sprite : nullptr;
    const bool available = skill ? entry && entry->usableNow : characterView_.attackUsable;
    imageAt(image, bounds, available ? WHITE : Color{255, 64, 64, 255});
}
void SceneView::drawControlPanel() const {
    const auto &player = characterView_;
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
    orb(false, player.hp / characterView_.maxLife);
    orb(true, player.mana / characterView_.maxMana);
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false));
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true));
    auto stamina = hudStamina();
    const float staminaFraction = std::clamp(player.stamina / characterView_.maxStamina, 0.f, 1.f);
    stamina.width *= staminaFraction;
    const bool blueStamina = player.blueStamina;
    // OpenDiablo2 HUD supplies ordinary/low stamina colors and alpha. Blue
    // uses its shared UI blue; the original 1.13c bar color remains unverified.
    const Color staminaColor = blueStamina ? Color{105, 105, 255, 200}
        : staminaFraction < .25f ? Color{255, 0, 0, 200} : Color{175, 136, 72, 200};
    DrawRectangleRec(stamina, staminaColor);
    if (player.nextLevelExperience && *player.nextLevelExperience > player.currentLevelExperience) {
        auto experience = hudExperience();
        const auto gained = player.experience - player.currentLevelExperience;
        const auto needed = *player.nextLevelExperience - player.currentLevelExperience;
        experience.width *= std::clamp(double(gained) / double(needed), 0.0, 1.0);
        experience.height = 2 * hudScale;
        DrawRectangleRec(experience, WHITE);
    }
    imageAt(assets_.runButton.frame(0, player.running ? 2 : 0), hudRunButton());
    imageAt(assets_.attributeButtons.frame(0, player.unspentAttributes > 0
        ? (view_.pointButtonPressed == false ? 1 : 0) : 2), hudCharacterButton());
    imageAt(assets_.attributeButtons.frame(0, player.unspentSkills > 0
        ? (view_.pointButtonPressed == true ? 1 : 0) : 2), hudSkillTreeButton());
    imageAt(assets_.miniPanelToggle.frame(0, view_.miniPanelOpen ? 2 : 0), hudMenuButton());
    if (view_.miniPanelOpen) {
        if (const auto *background = assets_.miniPanel.frame(0, 0))
            imageAt(background, hudMiniPanel(*background));
        constexpr int frames[] = {0, 2, 4, 8, 10, 12, 14};
        for (int index = 0; index < 7; ++index)
            if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]))
                imageAt(icon, hudMiniButton(*icon, index),
                        index == 4 ? Color{120, 120, 120, 255} : WHITE);
    }
}
std::optional<int> SceneView::miniPanelAt(Vec mouse) const {
    if (!view_.miniPanelOpen) return std::nullopt;
    const auto *background = assets_.miniPanel.frame(0, 0);
    if (!background || !CheckCollisionPointRec(rv(mouse), hudMiniPanel(*background)))
        return std::nullopt;
    constexpr int frames[] = {0, 2, 4, 8, 10, 12, 14};
    for (int index = 0; index < 7; ++index)
        if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]);
            icon && CheckCollisionPointRec(rv(mouse), hudMiniButton(*icon, index)))
            return index;
    return -1;
}
bool SceneView::leftSkillAllowed(int skill) const {
    const auto *entry = characterView_.skill(skill);
    return entry && entry->leftAllowed;
}
std::vector<std::optional<int>> SceneView::skillChoices(bool right) const {
    return characterView_.choices[unsigned(right)];
}
void SceneView::drawSkillControls(Vec mouse) const {
    if (view_.blocksWorld() || view_.inventory.drag || view_.inventory.split ||
        view_.inventory.goldDialog || view_.inventory.identify)
        return;
    std::optional<std::optional<int>> hovered;
    if (view_.skillPicker) {
        bool right = *view_.skillPicker;
        auto choices = skillChoices(right);
        for (size_t i = 0; i < choices.size(); ++i) {
            auto bounds = hudPickerSlot(right, int(i), int(choices.size()));
            drawSkillIcon(choices[i], bounds);
            const auto &hotkeys = characterView_.skillHotkeys;
            for (size_t key = 0; key < hotkeys.size(); ++key)
                if (hotkeys[key].right == right && hotkeys[key].skill == choices[i].value_or(-1)) {
                    auto label = "F" + std::to_string(key + 1);
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
        const auto *entry = choice ? characterView_.skill(*choice) : nullptr;
        auto name = entry ? entry->name : "Attack";
        const auto detailLines = entry ? entry->pickerTooltip : std::vector<std::string>{"Normal weapon attack"};
        const int width = std::min(520, W - 20);
        std::vector<std::string> wrapped;
        for (const auto &line : detailLines) {
            std::istringstream words(line);
            std::string word, current;
            while (words >> word) {
                const auto candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() && painter_.measure(candidate, 12) > width - 20) {
                    wrapped.push_back(current);
                    current = word;
                } else current = candidate;
            }
            if (!current.empty()) wrapped.push_back(current);
        }
        const int height = 30 + int(wrapped.size()) * 16;
        const float y = std::clamp(H - (view_.skillPicker ? 108 : 55) * hudScale - height - 8,
            5.f, float(std::max(5, H - height - 5)));
        DrawRectangle((W - width) / 2, int(y), width, height, {0, 0, 0, 225});
        painter_.centered(name, int(y + 7), 16, gold);
        for (size_t index = 0; index < wrapped.size(); ++index)
            painter_.centered(wrapped[index], int(y + 29 + index * 16), 12);
    }
    for (bool mana : {false, true}) {
        if (!CheckCollisionPointRec(rv(mouse), hudGlobe(mana)))
            continue;
        const auto &p = characterView_;
        auto text = std::string(mana ? "Mana: " : "Life: ") + std::to_string(int(mana ? p.mana : p.hp)) +
                    " / " + std::to_string(mana ? characterView_.maxMana : characterView_.maxLife);
        auto globe = hudGlobe(mana);
        painter_.label(text, int(globe.x + (globe.width - painter_.measure(text, 12)) / 2), int(globe.y - 20),
                       12, parchment);
    }
    std::string hint;
    if (CheckCollisionPointRec(rv(mouse), hudRunButton()))
        hint = "Walk / Run [SPACE]";
    if (CheckCollisionPointRec(rv(mouse), hudMenuButton()))
        hint = view_.miniPanelOpen ? "Close mini panel" : "Open mini panel";
    if (auto button = miniPanelAt(mouse); button && *button >= 0) {
        constexpr const char *labels[] = {"Character [C]", "Inventory [I]", "Skill Tree [S]",
                                          "Automap [TAB]", "Message unavailable", "Quest Log [Q]",
                                          "Game menu [Esc]"};
        hint = labels[*button];
    }
    if (CheckCollisionPointRec(rv(mouse), hudStamina()))
        hint = "Stamina: " + std::to_string(int(characterView_.stamina)) + " / " +
               std::to_string(characterView_.maxStamina);
    if (characterView_.unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(mouse), hudCharacterButton())) {
        hint = "New attribute points [A]";
    }
    if (characterView_.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(mouse), hudSkillTreeButton())) {
        hint = "New skill points [S]";
    }
    if (CheckCollisionPointRec(rv(mouse), hudExperience())) {
        hint = "Experience: " + std::to_string(characterView_.experience) + " / " +
               (characterView_.nextLevelExperience ? std::to_string(*characterView_.nextLevelExperience) : "MAX");
    }
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
