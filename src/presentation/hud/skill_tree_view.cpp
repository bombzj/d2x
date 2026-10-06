#include "presentation/scene_view.hpp"
#include "skill_tree.hpp"
#include <algorithm>
#include <sstream>

namespace d2x {
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
        const bool itemGranted = entry.baseRankKnown && entry.effectiveRankKnown && effective > value;
        const bool ready = entry.canAllocate;
        const auto &texture = image->second.sprite.texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds,
                       {0, 0}, 0, value || itemGranted ? WHITE : Color{92, 92, 92, 255});
        if (value || itemGranted || !entry.effectiveRankKnown) {
            auto count = entry.effectiveRankKnown ? std::to_string(effective) : "?";
            painter_.label(count, int(bounds.x + bounds.width - painter_.measure(count, 12) - 2),
                           int(bounds.y + bounds.height - 14), 12,
                           itemGranted ? Color{105, 105, 255, 255} : parchment);
        }
        if (CheckCollisionPointRec(rv(mouse), bounds))
            DrawRectangleLinesEx(bounds, 1, ready ? gold : Color{115, 106, 91, 255});
    }
    for (int page = 1; page <= 3; ++page) {
        auto bounds = skillTreeTab(page);
        const auto &name = characterView_.pageNames[page - 1];
        std::vector<std::string> lines;
        std::istringstream words(name);
        std::string word, line;
        constexpr int size = 13;
        while (words >> word) {
            auto candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && painter_.measure(candidate, size) > bounds.width - 8) {
                lines.push_back(line);
                line = word;
            } else line = std::move(candidate);
        }
        if (!line.empty()) lines.push_back(line);
        for (size_t index = 0; index < lines.size(); ++index)
            painter_.label(lines[index], int(bounds.x + (bounds.width - painter_.measure(lines[index], size)) / 2),
                           int(bounds.y + (bounds.height - lines.size() * 15) / 2 + index * 15), size,
                           page == view_.skillPage ? gold : parchment);
    }
    auto points = player.number("newskills", player.unspentSkills);
    auto pointBox = skillTreeRect(252, 54, 48, 26);
    painter_.label(points, int(pointBox.x + (pointBox.width - painter_.measure(points, 15)) / 2),
                   int(pointBox.y + 5), 15, gold);
    const auto close = skillTreeClose();
    const Color cross = CheckCollisionPointRec(rv(mouse), close)
        ? Color{232, 216, 179, 255} : Color{153, 150, 140, 255};
    const float inset = close.width * .27f;
    DrawLineEx({close.x + inset, close.y + inset},
               {close.x + close.width - inset, close.y + close.height - inset}, 3, cross);
    DrawLineEx({close.x + close.width - inset, close.y + inset},
               {close.x + inset, close.y + close.height - inset}, 3, cross);
    EndScissorMode();
    if (auto hovered = skillAt(mouse)) {
        const auto &lines = characterView_.skill(*hovered)->treeTooltip;
        const int width = std::min(360, W - 20);
        std::vector<std::string> wrapped;
        for (const auto &text : lines) {
            std::istringstream words(text);
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
        const int lineHeight = std::max(8, std::min(16, (H - 26) / std::max(1, int(wrapped.size()))));
        const int textSize = std::min(12, lineHeight - 1);
        const int height = lineHeight * int(wrapped.size()) + 16;
        const float left = std::clamp(panel.x - width - 8, 5.f, float(W - width - 5));
        const float top = std::clamp(mouse.y - 14, 5.f, float(std::max(5, H - height - 5)));
        auto box = Rectangle{left, top, float(width), float(height)};
        DrawRectangleRec(box, {0, 0, 0, 230});
        DrawRectangleLinesEx(box, 1, gold);
        for (size_t index = 0; index < wrapped.size(); ++index)
            painter_.label(wrapped[index], int(box.x + 10), int(box.y + 8 + index * lineHeight), textSize,
                index == 0 ? gold : parchment);
    }
}
} // namespace d2x
