#pragma once
#include "primitives.hpp"
#include "classic_panel.hpp"

namespace d2x {
inline Rectangle skillTreeBounds() { return classicPanelBounds(true); }
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
inline Rectangle skillTreeClose() { return skillTreeRect(259, 15, 27, 27); }
} // namespace d2x
