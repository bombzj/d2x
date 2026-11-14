#include "hireling_panel.hpp"
#include "presentation/graphics/text_list.hpp"
#include <algorithm>
#include <iterator>

namespace d2x {
namespace {
constexpr int hirelingRowGap = 7;
Rectangle hirelingListRow(int top, int lineCount) {
    const auto list = hirelingListContentBounds();
    // Strict native bounds at top + 12 and at the end of the text block.
    return {list.x + 1, list.y + 13 + top, list.width - 33, float(lineCount * textListLineStep - 1)};
}
}

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
    return classicInsetPanelBounds(false);
}

Rectangle hirelingPanelRect(float x, float y, float width, float height) {
    const auto panel = classicPanelBounds(false, 60);
    return {panel.x + x * classicPanelScale(), panel.y + y * classicPanelScale(),
            width * classicPanelScale(), height * classicPanelScale()};
}

// 1.13c 4A991/4A9B3 use inclusive bounds. The DC6's first visible row is one pixel below the hit rectangle.
Rectangle hirelingCloseArt() { return hirelingPanelRect(272, 386, 32, 32); }
Rectangle hirelingCloseHit() { return hirelingPanelRect(272, 385, 33, 33); }

Rectangle hirelingListBounds() {
    // 494B8 uses the game area height from 10E4E (screen height minus 40).
    return {float((W - 490) / 2), float((H - 40) / 2 - 195), 490, 350};
}

Rectangle hirelingListContentBounds() {
    auto panel = hirelingListBounds();
    panel.y += 35;
    panel.height = 280;
    return panel;
}

float hirelingListCancelBaseline() {
    // 495AB sets relative Y=315. The text-window renderer adds the
    // preceding title's 21-pixel advance (53941/539EC).
    return hirelingListBounds().y + 336;
}

int hirelingRowExtent(int lineCount) { return lineCount * textListLineStep + hirelingRowGap; }

void HirelingListLayout::clear(size_t expectedRows) {
    rows.clear();
    rows.reserve(expectedRows);
    lineCounts.clear();
    lineCounts.reserve(expectedRows);
}

void HirelingListLayout::add(int offer, std::vector<std::string> lines) {
    const int perPage = (textListHeight(hirelingListContentBounds()) - hirelingRowExtent(0)) / textListLineStep;
    for (int start = 0; start < int(lines.size()); start += perPage) {
        const int end = std::min(start + perPage, int(lines.size()));
        lineCounts.push_back(end - start);
        rows.push_back({offer, {std::make_move_iterator(lines.begin() + start), std::make_move_iterator(lines.begin() + end)}});
    }
}

int HirelingListLayout::lastPageFirst() const {
    return textListLastPage(lineCounts, textListHeight(hirelingListContentBounds()), hirelingRowGap);
}

int HirelingListLayout::pageSize(int first) const {
    return textListPageSize(lineCounts, first, textListHeight(hirelingListContentBounds()), hirelingRowGap);
}

int HirelingListLayout::hit(int first, Vec mouse) const {
    const auto bounds = hirelingListContentBounds();
    if (!CheckCollisionPointRec(rv(mouse), bounds)) return -1;
    for (int row = first, top = 0, end = first + pageSize(first); row < end; ++row) {
        const int lines = int(rows[size_t(row)].lines.size());
        const auto text = hirelingListRow(top, lines);
        if (mouse.x >= text.x && mouse.x < text.x + text.width && mouse.y >= text.y && mouse.y < text.y + text.height)
            return row;
        top += hirelingRowExtent(lines);
    }
    return -1;
}
} // namespace d2x
