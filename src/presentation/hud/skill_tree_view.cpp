#include "gameplay/session/session.hpp"
#include "presentation/scene_view.hpp"
#include "skill_tree.hpp"
#include <algorithm>
#include <sstream>

namespace d2x {
std::optional<int> SceneView::skillAt(Vec mouse) const {
    if (!view_.skillTreeOpen || !CheckCollisionPointRec(rv(mouse), skillTreeBounds()))
        return std::nullopt;
    for (const auto &[id, skill] : session_.content().skills.skills)
        if (skill.classCode == session_.characterCode() && skill.page == view_.skillPage &&
            CheckCollisionPointRec(rv(mouse), skillTreeNode(skill.row, skill.column)))
            return id;
    return std::nullopt;
}
void SceneView::drawSkillTree(Vec mouse) const {
    if (!view_.skillTreeOpen) return;
    const auto *tree = session_.content().skills.tree(session_.characterCode());
    if (!tree) return;
    auto art = assets_.skillTrees.find(tree->classCode);
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
    const auto &player = session_.state().player;
    for (const auto &[id, entry] : session_.content().skills.skills) {
        if (entry.classCode != tree->classCode || entry.page != view_.skillPage) continue;
        auto bounds = skillTreeNode(entry.row, entry.column);
        auto image = assets_.skillIcons.find(id);
        if (image == assets_.skillIcons.end()) continue;
        auto rank = player.skillRanks.find(id);
        const int value = rank == player.skillRanks.end() ? 0 : rank->second;
        const int effective = session_.effectiveSkillRank(id);
        const bool itemGranted = effective > value;
        const bool ready = session_.canAllocateSkill(id);
        const auto &texture = image->second.sprite.texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds,
                       {0, 0}, 0, value || itemGranted ? WHITE : Color{92, 92, 92, 255});
        if (value || itemGranted) {
            auto count = std::to_string(effective);
            painter_.label(count, int(bounds.x + bounds.width - painter_.measure(count, 12) - 2),
                           int(bounds.y + bounds.height - 14), 12,
                           itemGranted ? Color{105, 105, 255, 255} : parchment);
        }
        if (CheckCollisionPointRec(rv(mouse), bounds))
            DrawRectangleLinesEx(bounds, 1, ready ? gold : Color{115, 106, 91, 255});
    }
    for (int page = 1; page <= 3; ++page) {
        auto bounds = skillTreeTab(page);
        const auto &name = tree->pageNames[page - 1];
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
    auto points = std::to_string(player.unspentSkills);
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
        const auto &entry = *session_.content().skills.find(*hovered);
        auto rank = player.skillRanks.find(*hovered);
        const int baseRank = rank == player.skillRanks.end() ? 0 : rank->second;
        const int effective = session_.effectiveSkillRank(*hovered);
        std::string label = entry.name + "  " +
            std::to_string(baseRank) + "/" +
            std::to_string(entry.maximumRank);
        if (effective > baseRank)
            label += "  Item +" + std::to_string(effective - baseRank);
        std::vector<std::string> lines{label};
        if (!entry.description.empty()) lines.push_back(entry.description);
        if (player.level < session_.nextSkillRequiredLevel(*hovered))
            lines.push_back("Requires level " + std::to_string(session_.nextSkillRequiredLevel(*hovered)));
        for (int prerequisite : entry.prerequisites) {
            const auto learned = player.skillRanks.find(prerequisite);
            if (learned == player.skillRanks.end() || learned->second <= 0)
                if (const auto *required = session_.content().skills.find(prerequisite))
                    lines.push_back("Requires " + required->name);
        }
        if (entry.auraImplemented) {
            if (effective > 0) {
                lines.push_back("Current level " + std::to_string(effective));
                const auto details = auraSkillDetails(*hovered, effective);
                lines.insert(lines.end(), details.begin(), details.end());
            }
            if (baseRank < entry.maximumRank) {
                const int next = std::max(1, effective + 1);
                lines.push_back("Next level " + std::to_string(next));
                const auto details = auraSkillDetails(*hovered, next, true);
                lines.insert(lines.end(), details.begin(), details.end());
            }
        }
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
