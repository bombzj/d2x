#include "scene_view.hpp"
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
    DrawRectangle(int(panel.x) - 5, 0, int(panel.width) + 11, H - HUD, {0, 0, 0, 150});
    BeginScissorMode(int(panel.x), int(panel.y), int(panel.width), int(panel.height));
    auto layer = [&](int first) {
        for (int index = 0; index < 4; ++index) {
            const auto &tile = art->second.frames[first + index].texture;
            DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                {panel.x + (index % 2) * 256.f * 1.25f,
                 panel.y + (index / 2) * 256.f * 1.25f,
                 tile.width * 1.25f, tile.height * 1.25f}, {0, 0}, 0, WHITE);
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
        bool ready = player.unspentSkills > 0 && player.level >= entry.requiredLevel &&
                     value < entry.maximumRank;
        for (int prerequisite : entry.prerequisites)
            ready &= player.skillRanks.contains(prerequisite);
        const auto &texture = image->second.sprite.texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds,
                       {0, 0}, 0, value || itemGranted ? WHITE : Color{92, 92, 92, 255});
        if (value || itemGranted) {
            auto count = std::to_string(effective) + (itemGranted ? "*" : "");
            painter_.label(count, int(bounds.x + bounds.width - painter_.measure(count, 12) - 2),
                           int(bounds.y + bounds.height - 14), 12, parchment);
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
        std::string label = entry.name + "  " +
            std::to_string(rank == player.skillRanks.end() ? 0 : rank->second) + "/" +
            std::to_string(entry.maximumRank);
        if (session_.effectiveSkillRank(*hovered) >
            (rank == player.skillRanks.end() ? 0 : rank->second))
            label += "  Item +1";
        if (player.level < entry.requiredLevel)
            label += "  Requires level " + std::to_string(entry.requiredLevel);
        else if (std::any_of(entry.prerequisites.begin(), entry.prerequisites.end(),
                     [&](int prerequisite) { return !player.skillRanks.contains(prerequisite); }))
            label += "  Requires earlier skill";
        auto width = std::max(205, painter_.measure(label, 12) + 20);
        auto box = Rectangle{panel.x - width - 8, mouse.y - 14, float(width), 31};
        DrawRectangleRec(box, {0, 0, 0, 230});
        DrawRectangleLinesEx(box, 1, gold);
        painter_.label(label, int(box.x + 10), int(box.y + 8), 12, parchment);
    }
}
} // namespace d2x
