#include "presentation/scene_view.hpp"
#include "character_panel.hpp"
#include <algorithm>
#include <sstream>

namespace d2x {
namespace {
// Small labels use the original font6, not a reduced font16.
// Original D2Client label/value records use inclusive horizontal bounds and
// bottom-origin text baselines. Keep those coordinates and the DC6 offsets.
void characterText(const ClassicFont &font, const std::string &value, float left, float baseline, float right) {
    if (value.empty()) return;
    // Original two-line labels use baselines four pixels either side of the row.
    constexpr float lineStep = 8;
    const float scale = classicPanelScale;
    std::vector<std::string> lines;
    std::istringstream input(value);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    const auto box = characterArtRect(left, baseline - 20, right - left + 1, 40);
    BeginScissorMode(int(box.x), int(box.y), int(box.width), int(box.height));
    for (size_t row = 0; row < lines.size(); ++row) {
        float width = 0;
        for (unsigned char character : lines[row]) width += font.widths[character];
        const auto origin = characterArtRect(left + int(std::max(0.f, (right - left + 1 - width) * .5f)),
            baseline + (float(row) - (float(lines.size()) - 1) * .5f) * lineStep, 0, 0);
        float x = origin.x;
        for (unsigned char character : lines[row]) {
            const auto *glyph = font.glyphs.frame(0, font.indices[character]);
            if (glyph && character != ' ')
                DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                    {x + glyph->x * scale, origin.y + (glyph->y - glyph->texture.height) * scale,
                        glyph->texture.width * scale, glyph->texture.height * scale},
                    {}, 0, WHITE);
            x += font.widths[character] * scale;
        }
    }
    EndScissorMode();
}
std::string groupedExperience(std::string value) {
    if (value.find_first_not_of("0123456789") != std::string::npos) return value;
    for (int at = int(value.size()) - 3; at > 0; at -= 3) value.insert(size_t(at), 1, ',');
    return value;
}
}
void SceneView::drawCharacter(Vec mouse) const {
    if (!view_.characterOpen) return;
    const auto panel = characterBounds();
    drawPanelFrame(false);
    // The original tiles already contain the permanent gray stone value cells.
    for (int index = 0; index < 4; ++index) {
        const auto &tile = assets_.inventoryPanel.frames.at(size_t(index)).texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
            {panel.x + (index % 2) * 256 * classicPanelScale,
             panel.y + (index / 2) * 256 * classicPanelScale,
             tile.width * classicPanelScale, tile.height * classicPanelScale}, {}, 0, WHITE);
    }
    const auto art = [](const Sprite &sprite, Rectangle box) {
        DrawTexturePro(sprite.texture, {0, 0, float(sprite.texture.width), float(sprite.texture.height)},
            box, {}, 0, WHITE);
    };
    art(*assets_.questClose.frame(0, 10), characterClose());
    const auto label = [&](const char *key) -> const std::string & { return assets_.characterLabels.at(key); };
    const auto cell = [&](const std::string &value, float left, float baseline, float right,
                          bool small = false) {
        characterText(small ? assets_.characterLabelFont : assets_.font, value, left, baseline, right);
    };
    const auto &player = characterView_;
    // Original 1.13c D2Client RVA BD613: long names use font8/font6.
    const auto &nameFont = player.name.size() >= 13 ? assets_.characterLabelFont :
        player.name.size() >= 11 ? assets_.characterCompactFont : assets_.font;
    characterText(nameFont, player.name, 13, 25, 161);
    cell(player.className, 193, 25, 310);
    cell(label("strchrlvl"), 11, 44, 52, true);
    cell(player.number("level", player.level), 13, 59, 53);
    cell(label("strchrexp"), 65, 44, 180, true);
    cell(groupedExperience(player.number("experience", player.experience)), 67, 59, 180);
    const auto next = !player.nextLevelKnown ? "?" : player.nextLevelExperience
        ? groupedExperience(std::to_string(*player.nextLevelExperience)) : "MAX";
    cell(label("strchrnxtlvl"), 193, 44, 308, true);
    cell(next, 195, 59, 308);

    // Original 1.13c D2Client RVA DD5E0/DD6F0: label and value baselines.
    constexpr float attributeLabelY[]{97, 160, 245, 307};
    constexpr float attributeValueY[]{99, 161, 247, 308};
    constexpr const char *attributeLabels[]{"strchrstr", "strchrdex", "strchrvit", "strchreng"};
    constexpr const char *attributeStats[]{"strength", "dexterity", "vitality", "energy"};
    for (int index = 0; index < 4; ++index) {
        cell(label(attributeLabels[index]), 10, attributeLabelY[index], 73, true);
        cell(player.number(attributeStats[index], player.attributes[size_t(index)]), 77, attributeValueY[index], 112);
        if (player.unspentAttributes > 0 && !player.unknownStats.contains("statpts")) {
            const auto button = characterAddButton(index);
            const auto *socket = assets_.attributeSocket.frame(0, 0);
            art(*socket, {button.x - 3 * classicPanelScale, button.y - 2 * classicPanelScale,
                socket->texture.width * classicPanelScale, socket->texture.height * classicPanelScale});
            art(*assets_.attributeButtons.frame(0, 0), button);
        }
    }

    const auto actionRows = [&](std::optional<int> id, uint32_t owner,float damageY, float ratingY) {
        const auto *skill = player.skill(id.value_or(0),owner);
        const std::string name = skill ? skill->name : "?";
        const auto action = player.actionDisplay(id,owner);
        cell(action.damage.empty() ? name : name + '\n' + label("strchrskm"), 160, damageY, 259, true);
        cell(action.damage, 261, damageY + 2, 309);
        if (!action.attackRating.empty()) {
            auto caption = label("strchrrat");
            const auto placeholder = caption.find("%s");
            if (placeholder != std::string::npos) caption.replace(placeholder, 2, name);
            cell(caption, 160, ratingY, 269, true);
            cell(action.attackRating, 273, ratingY + 2, 308);
        }
    };
    actionRows(view_.leftSkill,player.selectedSkillOwners.at(player.weaponSet*2), 97, 159);
    actionRows(view_.rightSkill,player.selectedSkillOwners.at(player.weaponSet*2+1), 119, 181);
    const auto numberCell = [&](const char *stat, int value, float left, float baseline, float right) {
        const auto text = player.number(stat, value);
        const bool compact = text != "?" &&
            (value >= 1000 || UiPainter(assets_.font, 0).measure(text, 16) >= right - left);
        characterText(compact ? assets_.characterCompactFont : assets_.font, text, left, baseline, right);
    };
    cell(label("strchrdef"), 174, 207, 268, true);
    numberCell("armorclass", player.defense, 273, 209, 307);
    const auto resourceRow = [&](const char *caption, const char *maximumStat, int maximum,
                                 const char *currentStat, int current, float labelY, float valueY) {
        cell(label(caption), 174, labelY, 228, true);
        // Two native cells: maximum on the left, current on the right.
        numberCell(maximumStat, maximum, 232, valueY, 267);
        numberCell(currentStat, current, 273, valueY, 308);
    };
    resourceRow("strchrstm", "maxstamina", player.maxStamina, "stamina", int(player.stamina), 245, 246);
    resourceRow("strchrlif", "maxhp", player.maxLife, "hitpoints", int(player.hp), 269, 270);
    resourceRow("strchrman", "maxmana", player.maxMana, "mana", int(player.mana), 307, 308);
    constexpr const char *resistLabels[]{"strchrfir", "strchrcol", "strchrlit", "strchrpos"};
    constexpr const char *resistStats[]{"fireresist", "coldresist", "lightresist", "poisonresist"};
    constexpr float resistanceLabelY[]{346, 370, 395, 419};
    constexpr float resistanceValueY[]{348, 372, 396, 420};
    for (size_t index = 0; index < 4; ++index) {
        cell(label(resistLabels[index]), 190, resistanceLabelY[index], 268, true);
        const auto &valueFont = !player.unknownStats.contains(resistStats[index]) && player.resistances[index] < 0
            ? assets_.characterRedFont : assets_.font;
        characterText(valueFont, player.number(resistStats[index], player.resistances[index]),
            273, resistanceValueY[index], 307);
    }
    if (player.unspentAttributes > 0 && !player.unknownStats.contains("statpts")) {
        const auto *points = assets_.attributePoints.frame(0, 0);
        art(*points, characterArtRect(3, 341, float(points->texture.width), float(points->texture.height)));
        characterText(assets_.characterPointFont, label("strchrstat") + '\n' + label("strchrrema"), 11, 359, 89);
        cell(player.number("statpts", player.unspentAttributes), 92, 360, 129);
    }
    (void)mouse;
}
} // namespace d2x
