#pragma once
#include "presentation/graphics/primitives.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace d2x {
// Both skill surfaces use the original font16, centered lines and an icon anchor.
inline void drawSkillTooltip(const UiPainter &painter, const std::vector<std::string> &text, Vec anchor,
    const UiPainter *green = nullptr, size_t bonusHeading = std::numeric_limits<size_t>::max(),
    bool below = false, float iconHeight = 0) {
    constexpr int size = 16, lineHeight = 18, padding = 6;
    const int maximumWidth = W - 20;
    std::vector<std::string> lines;
    std::vector<bool> greenLines;
    for (size_t blockIndex = 0; blockIndex < text.size(); ++blockIndex) {
        const auto &block = text[blockIndex];
        auto append = [&](const std::string &line) {
            lines.push_back(line);
            greenLines.push_back(green && (blockIndex == 0 || blockIndex == bonusHeading));
        };
        size_t start = 0;
        do {
            const auto end = block.find('\n', start);
            auto paragraph = block.substr(start, end == std::string::npos ? end : end - start);
            if (!paragraph.empty() && paragraph.back() == '\r') paragraph.pop_back();
            if (painter.measure(paragraph, size) <= maximumWidth - 2 * padding) append(paragraph);
            else {
                std::istringstream words(paragraph);
                std::string word, current;
                while (words >> word) {
                    const auto candidate = current.empty() ? word : current + " " + word;
                    if (!current.empty() && painter.measure(candidate, size) > maximumWidth - 2 * padding) {
                        append(current);
                        current = word;
                    } else current = candidate;
                }
                if (!current.empty()) append(current);
            }
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start <= block.size());
    }
    if (lines.empty()) return;
    int width = 2 * padding;
    for (const auto &line : lines) width = std::max(width, painter.measure(line, size) + 2 * padding);
    width = std::min(width, maximumWidth);
    const int height = int(lines.size()) * lineHeight + 8;
    const int left = std::clamp(int(anchor.x) - width / 2, 5, W - width - 5);
    int preferredTop = below ? int(anchor.y) + 4 : int(anchor.y) - height - 4;
    if (below && preferredTop + height > H - 5)
        preferredTop = int(anchor.y - iconHeight) - height - 4;
    const int top = std::clamp(preferredTop, 5, std::max(5, H - height - 5));
    DrawRectangle(left, top, width, height, {0, 0, 0, 230});
    for (size_t index = 0; index < lines.size(); ++index)
        (greenLines[index] ? *green : painter).label(lines[index], left + (width - painter.measure(lines[index], size)) / 2,
                                                   top + 4 + int(index) * lineHeight, size, WHITE);
}
} // namespace d2x
