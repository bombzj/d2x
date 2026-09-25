#pragma once
#include "classic_panel.hpp"

namespace d2x {
inline Rectangle hirelingArtRect(float x, float y, float width, float height) {
    const auto panel = classicPanelBounds(false);
    return {panel.x + x * classicPanelScale, panel.y + y * classicPanelScale,
            width * classicPanelScale, height * classicPanelScale};
}
inline Rectangle hirelingClose() { return hirelingArtRect(277, 389, 32, 32); }
inline Rectangle hirelingListBounds() {
    return {W / 2.f - 300 * classicPanelScale, 20 * classicPanelScale,
            600 * classicPanelScale, 428 * classicPanelScale};
}
inline Rectangle hirelingListRow(int row) {
    auto p = hirelingListBounds();
    return {p.x + 25 * classicPanelScale, p.y + (51 + row * 40) * classicPanelScale,
            547 * classicPanelScale, 40 * classicPanelScale};
}
inline Rectangle hirelingListCancel() {
    auto p = hirelingListBounds();
    return {p.x, p.y + 388 * classicPanelScale, p.width, 40 * classicPanelScale};
}
inline Rectangle hirelingScrollBounds() {
    auto p = hirelingListBounds();
    return {p.x + 578 * classicPanelScale, p.y + 48 * classicPanelScale,
            10 * classicPanelScale, 330 * classicPanelScale};
}
inline constexpr int hirelingVisibleRows = 8;
} // namespace d2x
