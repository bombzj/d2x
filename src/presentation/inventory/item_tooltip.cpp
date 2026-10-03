#include "presentation/scene_view.hpp"
#include <algorithm>

namespace d2x {
void SceneView::itemButton(Rectangle bounds, const char *label, Color color) const {
    if (auto sprite = assets_.button.frame(0, 0)) {
        auto texture = sprite->texture;
        DrawTexturePro(texture, {0, 0, float(texture.width), float(texture.height)}, bounds, {0, 0}, 0,
                       WHITE);
    } else
        frame(bounds, color);
    painter_.label(label, int(bounds.x + (bounds.width - painter_.measure(label, 12)) / 2),
                   int(bounds.y + (bounds.height - 12) / 2), 12, color);
}
void SceneView::drawItemText(std::vector<ItemTextLine> lines, ItemQuality quality, Vec anchor,
                              std::optional<unsigned> price, std::string_view priceLabel) const {
    if (price) lines.insert(lines.begin() + 1, {std::string(priceLabel) + ": " + std::to_string(*price), ItemTextTone::Normal});
    std::vector<std::string> text;
    std::vector<Color> colors;
    for (auto &line : lines) {
        text.push_back(std::move(line.text));
        colors.push_back(line.tone == ItemTextTone::Name ? itemColor(quality)
            : line.tone == ItemTextTone::Property ? Color{105, 105, 255, 255}
            : line.tone == ItemTextTone::Error ? RED : WHITE);
    }
    int fontSize = 16, rowHeight = 20;
    size_t rowsPerColumn = 0, columns = 0;
    std::vector<int> widths;
    int width = 0;
    for (;;) {
        rowHeight = fontSize + 4;
        rowsPerColumn = size_t((H - HUD - 34) / rowHeight);
        columns = (text.size() + rowsPerColumn - 1) / rowsPerColumn;
        widths.assign(columns, 0);
        for (size_t index = 0; index < text.size(); ++index)
            widths[index / rowsPerColumn] = std::max(widths[index / rowsPerColumn],
                painter_.measure(text[index], fontSize) + 24);
        width = 0;
        for (int size : widths) width += size;
        if (width <= W - 16 || fontSize == 1) break;
        --fontSize;
    }
    const float height = float(std::min(text.size(), rowsPerColumn) * rowHeight + 18);
    Rectangle box{std::clamp(anchor.x - width, 8.f, std::max(8.f, W - width - 8.f)),
                  std::clamp(anchor.y - height - 12, 8.f, std::max(8.f, H - HUD - height - 8.f)),
                  float(width), height};
    DrawRectangleRec(box, {0, 0, 0, 225});
    int columnX = int(box.x);
    for (size_t column = 0; column < columns; ++column) {
        for (size_t row = 0; row < rowsPerColumn && column * rowsPerColumn + row < text.size(); ++row) {
            size_t i = column * rowsPerColumn + row;
            painter_.inBox(text[i], {float(columnX), box.y + 9 + float(row * rowHeight),
                                      float(widths[column]), float(rowHeight)}, fontSize, colors[i]);
        }
        columnX += widths[column];
    }
}
} // namespace d2x
