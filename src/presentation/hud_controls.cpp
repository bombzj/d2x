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
void SceneView::drawSkillIcon(std::optional<int> skill, Rectangle bounds) const {
    const auto &player = session_.state().player;
    const auto *entry = skill ? session_.content().skills.find(*skill) : nullptr;
    auto icon = skill ? assets_.skillIcons.find(*skill) : assets_.skillIcons.end();
    const auto *image = icon == assets_.skillIcons.end() ? &assets_.attackIcon : &icon->second.sprite;
    bool available = !player.dead && (!skill || (entry && session_.skillAvailable(*skill)));
    if (session_.region().definition.safe)
        available &= entry && entry->allowedInTown;
    auto effect = entry ? implementedSkillEffect(*entry) : std::nullopt;
    if (entry && entry->originalEffect && session_.effectiveSkillRank(*skill) > 0)
        available &= player.mana >= std::max(player.channelSkill() == *skill ? 0.f : float(entry->originalEffect->startMana), resolveOriginalSkill(*entry->originalEffect,
            session_.effectiveSkillRank(*skill), player.skillRanks, session_.fireMasteryPercent(),
            session_.lightningMasteryPercent()).manaCost);
    else if (effect) available &= player.mana >= skillDefinition(*effect).manaCost;
    imageAt(image, bounds, available ? WHITE : Color{255, 64, 64, 255});
    if (effect && (!entry || !entry->originalEffect) && player.cooldown[size_t(*effect)] > 0) {
        auto time = std::string(TextFormat("%.1f", player.cooldown[size_t(*effect)]));
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
    orb(false, player.hp / session_.characterStats().maxLife);
    orb(true, player.mana / session_.characterStats().maxMana);
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false));
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true));
    auto stamina = hudStamina();
    stamina.width *= std::clamp(player.stamina / session_.characterStats().maxStamina, 0.f, 1.f);
    DrawRectangleRec(stamina, {170, 136, 68, 175});
    const auto &thresholds = session_.experienceThresholds();
    const size_t level = size_t(player.level);
    if (level + 1 < thresholds.size() && thresholds[level + 1] > thresholds[level]) {
        auto experience = hudExperience();
        const auto gained = player.experience - thresholds[level];
        const auto needed = thresholds[level + 1] - thresholds[level];
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
    const auto *entry = session_.content().skills.find(skill);
    return entry && entry->leftAllowed;
}
std::vector<std::optional<int>> SceneView::skillChoices(bool right) const {
    std::vector<std::optional<int>> choices{std::nullopt}; // ordinary attack
    const auto *tree = session_.content().skills.tree(session_.characterCode());
    if (!tree) return choices;
    for (int id : tree->commonSkills) {
        const auto *entry = session_.content().skills.find(id);
        if (!entry || (right == false && !entry->leftAllowed)) continue;
        if (entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw" ||
            entry->sourceName == "Kick" || entry->sourceName == "Left Hand Swing" ||
            entry->sourceName == "Unsummon")
            choices.push_back(id);
    }
    for (const auto &[id, entry] : session_.content().skills.skills)
        if (entry.classCode == session_.characterCode() && !entry.passive &&
            (right || entry.leftAllowed) && session_.skillAvailable(id))
            choices.push_back(id);
    return choices;
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
            const auto &hotkeys = session_.state().player.skillHotkeys;
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
        auto entry = choice ? session_.content().skills.find(*choice) : nullptr;
        auto name = entry ? entry->name : "Attack";
        auto effect = entry ? implementedSkillEffect(*entry) : std::nullopt;
        std::string detail;
        if (entry && entry->originalEffect && session_.effectiveSkillRank(*choice) > 0) {
            const auto value = resolveOriginalSkill(*entry->originalEffect,
                session_.effectiveSkillRank(*choice), session_.state().player.skillRanks,
                session_.fireMasteryPercent(), session_.lightningMasteryPercent());
            detail = "Mana " + std::string(TextFormat("%.1f", value.manaCost));
            if (value.effect == Skill::Teleport) detail += " / Teleport to clear ground";
            else if (value.effect == Skill::Inferno)
                detail = "Mana/sec " + std::string(TextFormat("%.1f", value.manaCost * 12.5f)) +
                    " / Damage/sec " + std::string(TextFormat("%.1f-%.1f", value.minimumDamage * 25, value.maximumDamage * 25));
            else if (value.effect == Skill::FrozenArmor)
                detail += " / Defense +" + std::to_string(value.defensePercent) + "% / " +
                    std::to_string(int(value.buffDuration)) + " seconds";
            else if (value.effect == Skill::StaticField)
                detail += " / " + std::to_string(int(value.staticPercent)) + "% current life, range " +
                    std::to_string(int(value.staticRadius));
            else detail += " / Damage " + std::string(TextFormat("%.1f", value.minimumDamage)) +
                "-" + std::string(TextFormat("%.1f", value.maximumDamage));
        } else detail = !entry ? "Normal weapon attack" : effect
            ? std::string(skillDefinition(*effect).description)
            : entry->sourceName == "Throw" || entry->sourceName == "Left Hand Throw"
                ? "Throw equipped weapon; consumes one from the stack"
            : entry->sourceName == "Kick" || entry->sourceName == "Left Hand Swing"
                ? "Uses the current basic melee damage"
            : entry->sourceName == "Unsummon"
                ? "No summoned ally is available"
                : "Effect pending; enemy target uses normal attack";
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
                    " / " + std::to_string(mana ? session_.characterStats().maxMana : session_.characterStats().maxLife);
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
        hint = "Stamina: " + std::to_string(int(session_.state().player.stamina)) + " / " +
               std::to_string(session_.characterStats().maxStamina);
    if (session_.state().player.unspentAttributes > 0 &&
        CheckCollisionPointRec(rv(mouse), hudCharacterButton())) {
        hint = "New attribute points [A]";
    }
    if (session_.state().player.unspentSkills > 0 &&
        CheckCollisionPointRec(rv(mouse), hudSkillTreeButton())) {
        hint = "New skill points [S]";
    }
    if (CheckCollisionPointRec(rv(mouse), hudExperience())) {
        const auto &thresholds = session_.experienceThresholds();
        const size_t level = size_t(session_.state().player.level);
        hint = "Experience: " + std::to_string(session_.state().player.experience) + " / " +
               (level + 1 < thresholds.size() ? std::to_string(thresholds[level + 1]) : "MAX");
    }
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
