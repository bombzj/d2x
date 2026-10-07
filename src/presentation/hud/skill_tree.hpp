#pragma once
#include "presentation/graphics/primitives.hpp"
#include "classic_panel.hpp"
#include "contracts/character.hpp"

namespace d2x {
inline Rectangle skillTreeBounds() { return classicPanelBounds(true, 64); }
inline Rectangle skillTreeRect(float x, float y, float width, float height) {
    auto panel = skillTreeBounds();
    constexpr float scale = classicPanelScale;
    return {panel.x + x * scale, panel.y + y * scale, width * scale, height * scale};
}
inline Rectangle skillTreeNode(int row, int column) {
    return skillTreeRect(11.f + (column - 1) * 68.f, 12.f + (row - 1) * 68.f, 48, 48);
}
inline Rectangle skillTreeTab(int page) {
    return skillTreeRect(231, 110.f + (3 - page) * 107.f, 85, 105);
}
inline Rectangle skillTreeClose(const CharacterView &character, int page) {
    // SkillDesc supplies each page's occupied cells. The original button
    // uses a vacant outer column on the last row, or the middle column
    // when both outer columns are occupied; no per-class slot table.
    int lastRow = 0;
    for (const auto &[id, skill] : character.skills)
        if (skill.classCode == character.classCode && skill.page == page)
            lastRow = std::max(lastRow, skill.row);
    if (!lastRow) return {};
    std::array<bool, 3> occupied{};
    for (const auto &[id, skill] : character.skills)
        if (skill.classCode == character.classCode && skill.page == page && skill.row == lastRow &&
            skill.column >= 1 && skill.column <= 3)
            occupied[size_t(skill.column - 1)] = true;
    // Pixel anchors belong to the native UI layout, not an MPQ position table.
    constexpr float x[]{15,100,171};
    for (const int column : {0,2,1})
        if (!occupied[size_t(column)])
            return skillTreeRect(x[column], 385, 32, 32);
    return {};
}
} // namespace d2x
