#include "hud_layout.hpp"
#include "classic_hud.hpp"
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
    ClassicHudValues values;
    if (characterView_.maxLife > 0 && !player.unknownStats.contains("hitpoints")) values.life = player.hp / characterView_.maxLife;
    if (characterView_.maxMana > 0 && !player.unknownStats.contains("mana")) values.mana = player.mana / characterView_.maxMana;
    if (characterView_.maxStamina > 0 && !player.unknownStats.contains("stamina")) values.stamina = player.stamina / characterView_.maxStamina;
    values.running = player.running; values.blueStamina = player.blueStamina;
    values.attributePoints = player.unspentAttributes > 0; values.skillPoints = player.unspentSkills > 0;
    values.miniPanel = view_.miniPanelOpen; values.pressedPoint = view_.pointButtonPressed;
    if (!player.unknownStats.contains("experience") && !player.unknownStats.contains("level") &&
        player.nextLevelExperience && *player.nextLevelExperience > player.currentLevelExperience) {
        const auto gained = player.experience > player.currentLevelExperience
            ? player.experience - player.currentLevelExperience : 0;
        const auto needed = *player.nextLevelExperience - player.currentLevelExperience;
        values.experience = float(std::clamp(double(gained) / double(needed), 0.0, 1.0));
    }
    drawClassicHud({assets_.panel, assets_.orbs, assets_.globeOverlap, assets_.runButton,
                    assets_.attributeButtons, assets_.miniPanelToggle}, values);
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false));
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true));
    if (view_.miniPanelOpen) {
        if (const auto *background = assets_.miniPanel.frame(0, 0))
            imageAt(background, hudMiniPanel(*background));
        const std::vector<int> frames = multiplayer() ? std::vector<int>{0,2,4,6,8,10,12,14} : std::vector<int>{0,2,4,8,10,12,14};
        for (int index = 0; index < int(frames.size()); ++index)
            if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]))
                imageAt(icon, hudMiniButton(*icon, index),
                        (frames[index] == 6 || frames[index] == 10) ? Color{120, 120, 120, 255} : WHITE);
    }
}
std::optional<int> SceneView::miniPanelAt(Vec mouse) const {
    if (!view_.miniPanelOpen) return std::nullopt;
    const auto *background = assets_.miniPanel.frame(0, 0);
    if (!background || !CheckCollisionPointRec(rv(mouse), hudMiniPanel(*background)))
        return std::nullopt;
    const std::vector<int> frames = multiplayer() ? std::vector<int>{0,2,4,6,8,10,12,14} : std::vector<int>{0,2,4,8,10,12,14};
    for (int index = 0; index < int(frames.size()); ++index)
        if (const auto *icon = assets_.miniPanelButtons.frame(0, frames[index]);
            icon && CheckCollisionPointRec(rv(mouse), hudMiniButton(*icon, index)))
            return frames[index] == 6 ? 7 : frames[index] / 2 - (frames[index] >= 8 ? 1 : 0);
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
    if (view_.capturesWorldInput() || view_.inventory.drag || view_.inventory.split ||
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
        auto text = std::string(mana ? "Mana: " : "Life: ") + p.number(mana ? "mana" : "hitpoints", int(mana ? p.mana : p.hp)) +
                    " / " + p.number(mana ? "maxmana" : "maxhp", mana ? characterView_.maxMana : characterView_.maxLife);
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
        hint = *button == 7 ? "Party service unavailable" : labels[*button];
    }
    if (CheckCollisionPointRec(rv(mouse), hudStamina()))
        hint = "Stamina: " + characterView_.number("stamina", int(characterView_.stamina)) + " / " +
               characterView_.number("maxstamina", characterView_.maxStamina);
    if (characterView_.unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(mouse), hudCharacterButton())) {
        hint = "New attribute points [A]";
    }
    if (characterView_.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(mouse), hudSkillTreeButton())) {
        hint = "New skill points [S]";
    }
    if (CheckCollisionPointRec(rv(mouse), hudExperience())) {
        hint = "Experience: " + characterView_.number("experience", characterView_.experience) + " / " +
               (characterView_.nextLevelExperience ? std::to_string(*characterView_.nextLevelExperience) : "MAX");
    }
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
