#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include "skill_tree.hpp"
#include "skill_tooltip.hpp"
#include <algorithm>
#include <sstream>

namespace d2x {
namespace {
void drawTreeLabel(const ClassicFont &font, const std::string &text, Rectangle bounds) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) lines.push_back(std::move(line));
    for (size_t index = 0; index < lines.size(); ++index) {
        float width = 0;
        for (uint32_t c : utf8Decode(lines[index])) width += font.widthOf(c) * classicPanelScale;
        float x = bounds.x + (bounds.width - width) / 2;
        const float baseline = bounds.y + bounds.height / 2 +
            (float(index) * 16 - float(lines.size() - 1) * 8 + 6) * classicPanelScale;
        for (uint32_t c : utf8Decode(lines[index])) {
            const auto *glyph = font.glyphOf(c);
            if (glyph && c != ' ')
                DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                    {x + glyph->x * classicPanelScale,
                     baseline + (glyph->y - glyph->texture.height) * classicPanelScale,
                     glyph->texture.width * classicPanelScale, glyph->texture.height * classicPanelScale}, {}, 0, WHITE);
            x += font.widthOf(c) * classicPanelScale;
        }
    }
}
void drawSkillRank(const ClassicFont &font, const std::string &text, Rectangle icon, bool compact) {
    // Center the rank in the MPQ background's cell outside the icon. Keep
    // the original four-pixel adjustment for the compact two-digit font.
    float x = icon.x + icon.width + (compact ? 0 : 4) * classicPanelScale;
    const float baseline = icon.y + icon.height + 12 * classicPanelScale;
    for (uint32_t character : utf8Decode(text)) {
        const auto *glyph = font.glyphOf(character);
        if (glyph && character != ' ')
            DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                {x + glyph->x * classicPanelScale,
                 baseline + (glyph->y - glyph->texture.height) * classicPanelScale,
                 glyph->texture.width * classicPanelScale, glyph->texture.height * classicPanelScale}, {}, 0, WHITE);
        x += font.widthOf(character) * classicPanelScale;
    }
}
}
std::optional<int> SceneView::skillAt(Vec mouse) const {
    if (!view_.skillTreeOpen || !CheckCollisionPointRec(rv(mouse), skillTreeBounds()))
        return std::nullopt;
    for (const auto &[id, skill] : characterView_.skills)
        if (skill.classCode == characterView_.classCode && skill.page == view_.skillPage &&
            CheckCollisionPointRec(rv(mouse), skillTreeNode(skill.row, skill.column)))
            return id;
    return std::nullopt;
}
void SceneView::drawSkillTree(Vec mouse) const {
    if (!view_.skillTreeOpen) return;
    if (!characterView_.hasSkillTree) return;
    auto art = assets_.skillTrees.find(characterView_.classCode);
    if (art == assets_.skillTrees.end()) return;
    const auto panel = skillTreeBounds();
    drawPanelFrame(true);
    BeginScissorMode(int(panel.x), int(panel.y), int(panel.width), int(panel.height));
    auto layer = [&](int first) {
        for (int index = 0; index < 4; ++index) {
            const auto &tile = art->second.frames[first + index].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (index % 2) * 256.f * classicPanelScale,
                 panel.y + (index / 2) * 256.f * classicPanelScale,
                 tile.width * classicPanelScale, tile.height * classicPanelScale}, {0, 0}, 0, WHITE);
        }
    };
    layer(0);
    layer(4 + (view_.skillPage - 1) * 4);
    const auto &player = characterView_;
    for (const auto &[id, entry] : characterView_.skills) {
        if (entry.classCode != characterView_.classCode || entry.page != view_.skillPage) continue;
        auto bounds = skillTreeNode(entry.row, entry.column);
        auto image = assets_.skillIcons.find(id);
        if (image == assets_.skillIcons.end()) continue;
        const int value = entry.baseRank;
        const int effective = entry.effectiveRank;
        const bool ranksKnown = entry.baseRankKnown && entry.effectiveRankKnown;
        const bool itemGranted = ranksKnown && effective > value;
        const bool reduced = ranksKnown && effective < value;
        const bool noPoints = !player.unknownStats.contains("newskills") && player.unspentSkills == 0;
        const bool bright = entry.canAllocate || (noPoints && entry.effectiveRankKnown && effective > 0);
        const bool hovered = CheckCollisionPointRec(rv(mouse), bounds);
        const auto &texture = (hovered ? image->second.treeHovered :
            bright ? image->second.sprite : image->second.treeDisabled).texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds,
                       {0, 0}, 0, WHITE);
        if (effective > 0 || !entry.effectiveRankKnown) {
            auto count = entry.effectiveRankKnown ? std::to_string(effective) : "?";
            const bool compact = entry.effectiveRankKnown && effective > 9;
            const auto &font = compact
                ? (itemGranted ? assets_.skillLevelCompactBlueFont :
                   reduced ? assets_.skillLevelCompactRedFont : assets_.skillLevelCompactFont)
                : (itemGranted ? assets_.skillLevelBlueFont : reduced ? assets_.characterRedFont : assets_.font);
            drawSkillRank(font, count, bounds, compact);
        }
    }
    for (int page = 1; page <= 3; ++page) {
        auto bounds = skillTreeTab(page);
        const auto &name = characterView_.pageNames[page - 1];
        drawTreeLabel(assets_.font, name, bounds);
    }
    drawTreeLabel(assets_.font, assets_.characterLabels.at("StrSklTree1") + '\n' +
        assets_.characterLabels.at("StrSklTree2") + '\n' + assets_.characterLabels.at("StrSklTree3"),
        skillTreeRect(231, 7, 85, 48));
    auto points = player.number("newskills", player.unspentSkills);
    auto pointBox = skillTreeRect(252, 54, 48, 26);
    drawTreeLabel(assets_.font, points, pointBox);
    const auto close = skillTreeClose(characterView_, view_.skillPage);
    if (const auto *button = assets_.questClose.frame(0, 10))
        DrawTexturePro(button->texture, {0, 0, float(button->texture.width), float(button->texture.height)},
                       close, {}, 0, WHITE);
    EndScissorMode();
    if (auto hovered = skillAt(mouse)) {
        const auto &skill = *characterView_.skill(*hovered);
        std::vector<std::string> lines;
        SkillTooltipHeadings headings;
        for (const auto &notice : skill.treeStatusHeadings) {
            headings.statuses.push_back(lines.size());
            lines.push_back(notice);
            lines.push_back("");
        }
        headings.title = lines.size();
        lines.insert(lines.end(), skill.treeTooltip.begin(), skill.treeTooltip.end());
        if (!skill.treeBonusHeading.empty() || !skill.treeBonusTooltip.empty()) {
            lines.push_back("");
            if (!skill.treeBonusHeading.empty()) {
                headings.bonus = lines.size();
                lines.push_back(skill.treeBonusHeading);
            }
            lines.insert(lines.end(), skill.treeBonusTooltip.begin(), skill.treeBonusTooltip.end());
        }
        const auto bounds = skillTreeNode(skill.row, skill.column);
        const UiPainter green(assets_.skillGreenFont, 0);
        const UiPainter body(skill.treeTooltipUnavailable ? assets_.characterRedFont : assets_.font, 0);
        drawSkillTooltip(body, lines, {bounds.x + bounds.width / 2, bounds.y + bounds.height},
                         &green, headings, true, bounds.height);
    }
}
} // namespace d2x
