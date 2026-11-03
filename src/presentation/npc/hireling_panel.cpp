#include "hireling_panel.hpp"

namespace d2x {
Rectangle hirelingPortraitBounds(float offset) {
    return {15 * classicPanelScale() + offset, 19 * classicPanelScale(), 46 * classicPanelScale(), 41 * classicPanelScale()};
}

Rectangle hirelingLifeBounds(float offset) {
    return {15 * classicPanelScale() + offset, 14 * classicPanelScale(), 46 * classicPanelScale(), 5 * classicPanelScale()};
}

Rectangle hirelingPortraitHitBounds(float offset) {
    return {15 * classicPanelScale() + offset, 24 * classicPanelScale(), 46 * classicPanelScale(), 36 * classicPanelScale()};
}

Rectangle hirelingPanelBounds() {
    return {72 * classicPanelScale(), 54 * classicPanelScale(),
        320 * classicPanelScale(), 432 * classicPanelScale()};
}

Rectangle hirelingArtRect(float x, float y, float width, float height) {
    const auto panel = hirelingPanelBounds();
    return {panel.x + x * classicPanelScale(), panel.y + y * classicPanelScale(),
            width * classicPanelScale(), height * classicPanelScale()};
}

Rectangle hirelingClose() { return hirelingArtRect(275, 386, 32, 32); }

Rectangle hirelingListBounds() {
    return {W / 2.f - 300 * classicPanelScale(), 20 * classicPanelScale(),
            600 * classicPanelScale(), 428 * classicPanelScale()};
}

Rectangle hirelingListRow(int row) {
    auto p = hirelingListBounds();
    return {p.x + 25 * classicPanelScale(), p.y + (51 + row * 40) * classicPanelScale(),
            547 * classicPanelScale(), 40 * classicPanelScale()};
}

Rectangle hirelingListCancel() {
    auto p = hirelingListBounds();
    return {p.x, p.y + 388 * classicPanelScale(), p.width, 40 * classicPanelScale()};
}

Rectangle hirelingScrollBounds() {
    auto p = hirelingListBounds();
    return {p.x + 578 * classicPanelScale(), p.y + 48 * classicPanelScale(),
            10 * classicPanelScale(), 330 * classicPanelScale()};
}
} // namespace d2x
