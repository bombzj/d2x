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
    if (!priceLabel.empty()) lines.insert(lines.begin(), {std::string(priceLabel) + ": " + (price ? std::to_string(*price) : "?"), ItemTextTone::Normal});
    std::vector<std::string> text;
    std::vector<ItemTextLine> rows;
    for (const auto &line : lines) {
        size_t start = 0;
        do {
            const auto end = line.text.find('\n', start);
            auto part = line.text.substr(start, end == std::string::npos ? end : end - start);
            if (!part.empty() && part.back() == '\r') part.pop_back();
            if (!part.empty() || end != std::string::npos || line.text.empty()) {
                text.push_back(part);
                rows.push_back({std::move(part), line.tone, line.propertyOffset != std::string::npos && line.propertyOffset >= start
                    ? line.propertyOffset - start : std::string::npos});
            }
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start < line.text.size());
    }
    if (text.empty()) return;
    auto fontIndex = [&](ItemTextTone tone) -> size_t {
        switch (tone) {
        case ItemTextTone::Name:
            switch (quality) {
            case ItemQuality::Magic: return 3;
            case ItemQuality::Rare: return 9;
            case ItemQuality::Set: return 2;
            case ItemQuality::Unique: return 4;
            case ItemQuality::Crafted: case ItemQuality::Tempered: return 8;
            default: return 0;
            }
        case ItemTextTone::RunewordName: return 4;
        case ItemTextTone::SocketedName: return 5;
        case ItemTextTone::Property: return 3;
        case ItemTextTone::Error: return 1;
        case ItemTextTone::SetProperty: return 2;
        case ItemTextTone::RuneName: return 8;
        default: return 0;
        }
    };
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
    // D2Win's framed tooltip uses a black DRAWMODE_TRANS75 rectangle.
    DrawRectangleRec(box, {0, 0, 0, 191});
    int columnX = int(box.x);
    for (size_t column = 0; column < columns; ++column) {
        for (size_t row = 0; row < rowsPerColumn && column * rowsPerColumn + row < text.size(); ++row) {
            size_t i = column * rowsPerColumn + row;
            const UiPainter painter(assets_.itemTextFonts[fontIndex(rows[i].tone)]);
            const Rectangle bounds{float(columnX), box.y + 9 + float(row * rowHeight), float(widths[column]), float(rowHeight)};
            if (rows[i].propertyOffset == std::string::npos || rows[i].propertyOffset >= text[i].size())
                painter.inBox(text[i], bounds, fontSize, WHITE);
            else {
                // Both runs share a baseline measured from the complete line.
                float top = 0, bottom = float(fontSize);
                bool found = false;
                for (unsigned char ch : text[i]) {
                    const auto *glyph = assets_.font.glyphs.frame(0, assets_.font.indices[ch]);
                    if (!glyph || ch == ' ' || !glyph->hitHeight) continue;
                    const float start = (glyph->hitY - glyph->y) * fontSize / 16.f;
                    const float end = start + glyph->hitHeight * fontSize / 16.f;
                    top = found ? std::min(top, start) : start;
                    bottom = found ? std::max(bottom, end) : end;
                    found = true;
                }
                const auto label = text[i].substr(0, rows[i].propertyOffset);
                const int x = int(bounds.x + (bounds.width - painter.measure(text[i], fontSize)) / 2);
                const int y = int(bounds.y + (bounds.height - (bottom - top)) / 2 - top);
                painter.label(label, x, y, fontSize, WHITE);
                UiPainter(assets_.itemTextFonts[3]).label(text[i].substr(rows[i].propertyOffset),
                    x + painter.measure(label, fontSize), y, fontSize, WHITE);
            }
        }
        columnX += widths[column];
    }
}
} // namespace d2x
