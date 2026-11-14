#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include "skill_tree.hpp"
#include "skill_tooltip.hpp"
#include <algorithm>
#include <sstream>

namespace d2x {
namespace {
void drawTreeText(const ClassicFont &font, const std::string &text, Vec origin) {
    const float scale = classicPanelScale();
    for (uint32_t c : utf8Decode(text)) {
        const auto *glyph = font.glyphOf(c);
        if (glyph && c != ' ')
            DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                {origin.x + glyph->x * scale, origin.y + (glyph->y - glyph->texture.height) * scale,
                 glyph->texture.width * scale, glyph->texture.height * scale}, {}, 0, WHITE);
        origin.x += font.widthOf(c) * scale;
    }
}
void drawTreeLabel(const ClassicFont &font, const std::string &text, Rectangle bounds) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) lines.push_back(std::move(line));
    for (size_t index = 0; index < lines.size(); ++index) {
        float width = 0;
        for (uint32_t c : utf8Decode(lines[index])) width += font.widthOf(c) * classicPanelScale();
        float x = bounds.x + (bounds.width - width) / 2;
        const float baseline = bounds.y + bounds.height / 2 +
            (float(index) * 16 - float(lines.size() - 1) * 8 + 6) * classicPanelScale();
        drawTreeText(font, lines[index], {x, baseline});
    }
}
void drawSkillRank(const ClassicFont &font, const std::string &text, Rectangle icon, bool compact) {
    // Center the rank in the MPQ background's cell outside the icon. Keep
    // the original four-pixel adjustment for the compact two-digit font.
    float x = icon.x + icon.width + (compact ? 0 : 4) * classicPanelScale();
    // 78A81 adds 12 to the inclusive DC6 bottom row, not the rectangle end.
    const float baseline = icon.y + icon.height + 11 * classicPanelScale();
    for (uint32_t character : utf8Decode(text)) {
        const auto *glyph = font.glyphOf(character);
        if (glyph && character != ' ')
            DrawTexturePro(glyph->texture, {0, 0, float(glyph->texture.width), float(glyph->texture.height)},
                {x + glyph->x * classicPanelScale(),
                 baseline + (glyph->y - glyph->texture.height) * classicPanelScale(),
                 glyph->texture.width * classicPanelScale(), glyph->texture.height * classicPanelScale()}, {}, 0, WHITE);
        x += font.widthOf(character) * classicPanelScale();
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
    uiScissor(panel.x, panel.y, panel.width, panel.height);
    auto layer = [&](int first) {
        for (int index = 0; index < 4; ++index) {
            const auto &tile = art->second.frames[first + index].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (index % 2) * 256.f * classicPanelScale(),
                 panel.y + (index / 2) * 256.f * classicPanelScale(),
                 tile.width * classicPanelScale(), tile.height * classicPanelScale()}, {0, 0}, 0, WHITE);
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
        const auto &texture = (hovered && view_.skillTreePressed == id ? image->second.treePressed :
            hovered ? image->second.treeHovered :
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
    // D2Client 1.13c 78330: 91-pixel heading span, baselines 85/97/109
    // at 800x600. Integer centering and native glyph origins are significant.
    for (int line = 0; line < 3; ++line) {
        const auto &text = assets_.characterLabels.at("StrSklTree" + std::to_string(line + 1));
        int width = 0;
        for (uint32_t c : utf8Decode(text)) width += assets_.font.widthOf(c);
        const auto anchor = skillTreeRect(230 + std::max(0, (91 - width) / 2), 24 + line * 12, 0, 0);
        drawTreeText(assets_.font, text, {anchor.x, anchor.y});
    }
    auto points = player.number("newskills", player.unspentSkills);
    // 78CD9..78D02: left-aligned at W-80-52, baseline H-60-400.
    // The original does not vertically/horizontally center this number in a box.
    const auto pointAnchor = skillTreeRect(268, 79, 0, 0);
    drawTreeText(assets_.font, points, {pointAnchor.x, pointAnchor.y});
    const auto close = skillTreeClose(characterView_, view_.skillPage);
    drawClassicSpriteButton(assets_.questClose, 10, close, close, mouse,
        view_.skillTreePressed == -1 && classicButtonHovered(close, mouse), nullptr);
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
        const auto &body = skill.treeTooltipUnavailable ? assets_.characterRedFont : assets_.font;
        drawSkillTooltip(body, lines, {bounds.x + bounds.width / 2, bounds.y + bounds.height},
                         &assets_.skillGreenFont, headings, true, bounds.height);
    }
}
} // namespace d2x
