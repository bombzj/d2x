#include "original_menu.hpp"
#include "core/text.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace d2x {
namespace {
uint32_t menuCharacter(uint32_t c) {
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}
std::string menuUpper(std::string_view text) {
    std::string out;
    for (uint32_t c : utf8Decode(text)) {
        c = menuCharacter(c);
        if (c < 0x80) out.push_back(char(c));
        else if (c < 0x800) { out.push_back(char(0xC0 | (c >> 6))); out.push_back(char(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back(char(0xE0 | (c >> 12))); out.push_back(char(0x80 | ((c >> 6) & 0x3F))); out.push_back(char(0x80 | (c & 0x3F))); }
        else { out.push_back(char(0xF0 | (c >> 18))); out.push_back(char(0x80 | ((c >> 12) & 0x3F))); out.push_back(char(0x80 | ((c >> 6) & 0x3F))); out.push_back(char(0x80 | (c & 0x3F))); }
    }
    return out;
}
}
OriginalMenu::OriginalMenu(Archives &archives, const ClassicFont &font)
    : graphics_(archives, "data/global/palette/sky/pal.dat"), font_(font) {
    try {
        border_ = graphics_.single("data/global/ui/menu/boxpieces.dc6");
        const auto palette = archives.read("data/global/palette/sky/pal.pl2");
        constexpr size_t shifts = 0x6B600 + 13*3;
        const auto *art = graphics_.animation("data/local/font/" + archives.languageFontDirectory + "/font16.dc6");
        if (!font.ready || border_.count < 22 || !art || palette.size() < shifts+5*256)
            throw std::runtime_error("Original menu artwork or font transforms are missing");
        for (const auto &[color, target] : {std::pair{3, &selectedFont_}, std::pair{4, &titleFont_}}) {
            *target = font_;
            target->graphics = &graphics_;
            target->source = art;
            target->sprites.clear();
            target->uploaded.clear();
            for (size_t i = 0; i < target->transform.size(); ++i)
                target->transform[i] = palette[shifts+size_t(color)*256+i];
            target->transformed = true;
        }
        ready_ = true;
    } catch (const std::exception &error) { reason_ = error.what(); }
}
float OriginalMenu::measure(std::string_view text) const {
    if (ttfTextReady()) {
        const auto upper = menuUpper(text);
        return ttfMeasure(upper, 16.f, 1.f).x;
    }
    float width{};
    for (uint32_t c : utf8Decode(text)) width += font_.widthOf(menuCharacter(c));
    return width;
}
Vec OriginalMenu::size(std::string_view title, std::span<const std::string> options) const {
    float width = measure(title);
    for (const auto &option : options) width = std::max(width, measure(option));
    return {width+24, 14+20*float(options.size()+!title.empty())};
}
Rectangle OriginalMenu::bounds(Vec topCentre, Rectangle viewport, std::string_view title,
    std::span<const std::string> options) const {
    const auto extent = size(title, options);
    const float width = std::min(extent.x, viewport.width), height = std::min(extent.y, viewport.height);
    return {std::clamp(topCentre.x-width/2, viewport.x, viewport.x+viewport.width-width),
        std::clamp(topCentre.y, viewport.y, viewport.y+viewport.height-height), width, height};
}
Rectangle OriginalMenu::row(Rectangle box, bool title, size_t index) const {
    return {box.x+5, box.y+6+20*float(index+title), box.width-10, 20};
}
int OriginalMenu::hit(Rectangle box, bool title, size_t count, Vec mouse) const {
    for (size_t index = 0; index < count; ++index)
        if (CheckCollisionPointRec(rv(mouse), row(box, title, index))) return int(index);
    return -1;
}
void OriginalMenu::label(std::string_view text, Rectangle box, int color) const {
    if (ttfTextReady()) {
        const auto upper = menuUpper(text);
        const float width = ttfMeasure(upper, 16.f, 1.f).x;
        const Color c = color == 3 ? gold : color == 4 ? WHITE : WHITE;
        ttfDraw(upper, box.x + (box.width - width) / 2, box.y, 16.f, 1.f, c);
        return;
    }
    if (!ready_) return;
    const ClassicFont &painter = color == 3 ? selectedFont_ : color == 4 ? titleFont_ : font_;
    float x = box.x+(box.width-measure(text))/2;
    for (uint32_t character : utf8Decode(text)) {
        const auto c = menuCharacter(character);
        const auto *glyph = painter.glyphOf(c);
        if (glyph && c != ' ') DrawTexturePro(glyph->texture,
            {0,0,float(glyph->texture.width),float(glyph->texture.height)},
            {x,box.y,float(glyph->texture.width),float(glyph->texture.height)},{0,0},0,WHITE);
        x += font_.widthOf(c);
    }
}
void OriginalMenu::draw(Rectangle box, std::string_view title, std::span<const std::string> options,
    int selected, std::string_view status) const {
    if (!ready_) return;
    DrawRectangleRec(box,{0,0,0,150}); originalBox(border_,box,1.f);
    if (!title.empty()) label(title,{box.x,box.y+6,box.width,20},4);
    for (size_t index = 0; index < options.size(); ++index)
        label(options[index],row(box,!title.empty(),index),selected == int(index) ? 3 : 0);
    if (!status.empty()) label(status,{box.x,box.y+box.height+5,box.width,20},4);
}
}
