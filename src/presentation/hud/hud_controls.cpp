#include "hud_layout.hpp"
#include "classic_hud.hpp"
#include "skill_tooltip.hpp"
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
void SceneView::drawSkillIcon(std::optional<int> skill, Rectangle bounds, bool picker,uint32_t owner) const {
    const auto *entry = skill ? characterView_.skill(*skill,owner) : nullptr;
    auto icon = skill ? assets_.skillIcons.find(*skill) : assets_.skillIcons.end();
    const auto *image = !skill ? &assets_.attackIcon
                              : icon != assets_.skillIcons.end() ? &icon->second.sprite : nullptr;
    const bool available = skill ? entry && (picker ? entry->pickerEnabled : entry->usableNow)
                                : picker ? !characterView_.dead : characterView_.attackUsable;
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
    drawSkillIcon(view_.leftSkill, hudSkillSlot(false),false,player.selectedSkillOwners[player.weaponSet*2]);
    drawSkillIcon(view_.rightSkill, hudSkillSlot(true),false,player.selectedSkillOwners[player.weaponSet*2+1]);
    if (view_.miniPanelOpen) {
        if (const auto *background = assets_.miniPanel.frame(0, 0))
            imageAt(background, hudMiniPanel(*background));
        const std::vector<int> frames = std::vector<int>{0,2,4,6,8,10,12,14};
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
    const std::vector<int> frames = std::vector<int>{0,2,4,6,8,10,12,14};
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
std::vector<SceneView::SkillPickerSlot> SceneView::skillPickerSlots(bool right) const {
    const auto &choices = characterView_.choices[unsigned(right)];
    std::map<int, std::vector<std::optional<int>>> rows;
    for (auto choice : choices) {
        if (!choice) {
            rows[0].push_back(choice);
            continue;
        }
        // Ordinary attack already has the null selection; hidden actions such
        // as Kick and Left Hand Swing have ListRow=-1 in the original table.
        const auto *entry = characterView_.skill(*choice);
        if (*choice == 0 || !entry || entry->listRow < 0) continue;
        if (entry->listPool > 0 && std::ranges::any_of(choices, [&](auto other) {
            const auto *candidate = other ? characterView_.skill(*other) : nullptr;
            return candidate && candidate->listPool == 0 && candidate->listRow == entry->listRow &&
                candidate->classCode == entry->classCode && candidate->iconCell == entry->iconCell;
        })) continue;
        rows[entry->listRow].push_back(choice);
    }
    std::vector<SkillPickerSlot> slots;
    int visibleRow = 0;
    for (auto &[row, skills] : rows) {
        std::ranges::sort(skills, {}, [](auto skill) { return skill.value_or(0); });
        for (size_t column = 0; column < skills.size(); ++column)
            slots.push_back({skills[column], hudPickerSlot(right, int(column), visibleRow)});
        ++visibleRow;
    }
    int column=0;
    for(const auto &skill:characterView_.chargedSkills) if(!skill.passive && (right || skill.leftAllowed) && skill.listRow>=0) {
        slots.push_back({skill.id,hudPickerSlot(right,column++,visibleRow),skill.owner});
        if(column>=8) {column=0;++visibleRow;}
    }
    return slots;
}
void SceneView::drawSkillControls(Vec mouse) const {
    if (view_.capturesWorldInput() || view_.inventory.drag || view_.inventory.split ||
        view_.inventory.goldDialog || view_.inventory.identify)
        return;
    std::optional<std::optional<int>> hovered;uint32_t hoveredOwner=UINT32_MAX;
    Vec tooltipAnchor{W / 2.f, H - 55 * hudScale};
    if (view_.skillPicker) {
        bool right = *view_.skillPicker;
        for (const auto &slot : skillPickerSlots(right)) {
            const auto bounds = slot.bounds;
            drawSkillIcon(slot.skill, bounds, true,slot.owner);
            const auto &hotkeys = characterView_.skillHotkeys;
            for (size_t key = 0; key < hotkeys.size(); ++key)
                if (hotkeys[key].right == right && hotkeys[key].skill == slot.skill.value_or(-1) && hotkeys[key].owner==slot.owner) {
                    auto label = "F" + std::to_string(key + 1);
                    painter_.label(label, int(bounds.x + 3), int(bounds.y + bounds.height - 12), 10, gold);
                }
            if (CheckCollisionPointRec(rv(mouse), bounds)) {
                hovered.emplace(slot.skill);hoveredOwner=slot.owner;
                tooltipAnchor = {bounds.x + bounds.width / 2, bounds.y};
                DrawRectangleLinesEx(bounds, 1, gold);
            }
        }
    } else {
        for (bool right : {false, true}) {
            const auto bounds = hudSkillSlot(right);
            if (CheckCollisionPointRec(rv(mouse), bounds)) {
                hovered.emplace(right ? view_.rightSkill : view_.leftSkill);hoveredOwner=characterView_.selectedSkillOwners[characterView_.weaponSet*2+unsigned(right)];
                tooltipAnchor = {bounds.x + bounds.width / 2, bounds.y};
            }
        }
    }
    if (hovered) {
        auto choice = *hovered;
        const auto *entry = choice ? characterView_.skill(*choice,hoveredOwner) : nullptr;
        auto name = entry ? entry->name : "Attack";
        auto lines = std::vector<std::string>{name};
        const auto details = entry ? entry->pickerTooltip : std::vector<std::string>{"Normal weapon attack"};
        lines.insert(lines.end(), details.begin(), details.end());
        drawSkillTooltip(UiPainter(assets_.font, 0), lines, tooltipAnchor);
    }
    for (bool mana : {false, true}) {
        if (!CheckCollisionPointRec(rv(mouse), hudGlobe(mana)))
            continue;
        const auto &p = characterView_;
        auto text = assets_.globeTextFormats[mana ? 1 : 0];
        size_t next = 0;
        for (const auto &value : {
                 p.number(mana ? "mana" : "hitpoints", int(mana ? p.mana : p.hp)),
                 p.number(mana ? "maxmana" : "maxhp", mana ? p.maxMana : p.maxLife)}) {
            next = text.find("%d", next);
            text.replace(next, 2, value);
            next += value.size();
        }
        // D2Client 1.13c RVA 0x276EE/0x277AC: white color 0, baseline H-95,
        // centers 65 (Life) and W-80 (Mana). Anchors follow the HUD artwork;
        // glyphs keep their original size rather than the widened HUD scale.
        const UiPainter nativeFont(assets_.font, 0);
        const float center = mana ? W - 80 * hudScale : 65 * hudScale;
        const float baseline = H - 95 * hudScale;
        float x = center - nativeFont.measure(text, 16) * hudTextScale / 2;
        for (unsigned char character : text) {
            const auto *glyph = assets_.font.glyphs.frame(0, assets_.font.indices[character]);
            if (glyph && character != ' ')
                DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                    {x + glyph->x * hudTextScale, baseline + (glyph->y - glyph->texture.height) * hudTextScale,
                     glyph->texture.width * hudTextScale, glyph->texture.height * hudTextScale}, {}, 0, WHITE);
            x += assets_.font.widths[character] * hudTextScale;
        }
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
               (!characterView_.nextLevelKnown ? "?" : characterView_.nextLevelExperience ? std::to_string(*characterView_.nextLevelExperience) : "MAX");
    }
    if (!hint.empty())
        painter_.centered(hint, H - HUD - 24, 12, parchment);
}
} // namespace d2x
