#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include "core/utf8.hpp"
#include <algorithm>

namespace d2x {
void SceneView::drawCorpseLabel(const std::string &label, Vec p) const {
    drawInteractionLabel(label, p, 40);
}
void SceneView::drawInteractionLabel(const std::string &label, Vec p, int offset) const {
    // Nameless placeholder NPCs carry no label text; skip the backdrop too.
    if (label.empty()) return;
    const int width = painter_.measure(label, 16);
    DrawRectangle(int(p.x - width / 2 - 8), int(p.y - offset), width + 16, 24, {0, 0, 0, 210});
    painter_.label(label, int(p.x - width / 2), int(p.y - offset + 5), 16, WHITE);
}
void SceneView::drawInteractionDetails(const std::vector<std::string> &lines,Vec p) const {
    if(lines.empty()) return;
    int width=0;
    for(const auto &line:lines) width=std::max(width,painter_.measure(stripD2ColorCodes(line),16));
    width=std::min(width,W-16);
    const int height=int(lines.size())*24;
    const int x=std::clamp(int(p.x)-width/2-8,0,W-width-16);
    const int y=std::max(0,int(p.y)-70-height+24);
    DrawRectangle(x,y,width+16,height,{0,0,0,210});
    for(size_t row=0;row<lines.size();++row)
        drawColoredItemText(lines[row],0,{float(x+8),float(y+int(row)*24),float(width),24},16);
}
void SceneView::drawPlayerLabel(const std::string &label, Vec p, bool hostile) const {
    if (label.empty()) return;
    const UiPainter name(hostile ? assets_.characterRedFont : assets_.font);
    const int width = name.measure(label, 16);
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    if (!blend) return;
    // Player labels have a red translucent backing independently of relation;
    // only the lettering changes with the confirmed directed hostile flag.
    blend->drawRectangle({p.x - width / 2.f - 4, p.y - 70, float(width + 8), 24}, assets_.playerNameBackground);
    name.label(label, int(p.x - width / 2.f), int(p.y - 65), 16, WHITE);
}
void SceneView::drawOverheadText(const std::string &text,Vec p) {
    const auto decoded=strictUtf8(text);
    if(!decoded || decoded->empty() || !std::all_of(decoded->begin(),decoded->end(),[](uint32_t c){return c>=32 && c<=0xffff;})) return;
    const auto *font=assets_.ensureOverheadFont();if(!font) return;
    const auto viewport=worldViewport();
    if(p.y<=viewport.y || p.y>=viewport.y+viewport.height ||
        p.x<=viewport.x-100 || p.x>=viewport.x+viewport.width+100) return;
    std::vector<std::vector<uint32_t>> lines;int width=0;
    const auto &message=*decoded;
    size_t start=0;
    while(start<message.size()) {
        size_t end=start,space=start;int used=0;
        while(end<message.size()) {
            const int advance=font->widthOf(message[end]);
            if(end>start && used+advance>180) break;
            used+=advance;if(message[end]==' ') space=end;++end;
        }
        if(end<message.size() && space>start) end=space;
        std::vector<uint32_t> line(message.begin()+start,message.begin()+end);int measured=0;
        for(const auto c:line) measured+=font->widthOf(c);
        width=std::max(width,measured);lines.push_back(std::move(line));
        start=end;while(start<message.size() && message[start]==' ') ++start;
    }
    if(lines.empty()) return;
    // Retail 7DC00 -> 7CA40 -> 7B240: the native chat font, 180px
    // wrapping, 20px horizontal padding, 15px rows and DrawBox mode 1.
    const int left=int(p.x)-(width+20)/2;
    const int top=int(p.y)-30-50-int(lines.size())*15-3;
    const int height=int(lines.size())*15+6;
    if(top<viewport.y || top+height>=viewport.y+viewport.height) return;
    const int palette=std::clamp(mapView().palette,0,4);
    const auto *blend=palette==0?paletteBlend_.get():actPaletteBlends_[size_t(palette)].get();
    if(!blend) return;
    blend->drawRectangle({float(left),float(top+4),float(width+20),float(height-5)},BLACK,true);
    const auto visible=std::min(lines.size(),size_t{10});
    for(size_t row=0;row<visible;++row) {
        int x=left+10;const int baseline=top+18+int(lines.size()-visible+row)*15;
        for(const auto c:lines[row]) {
            const auto *glyph=font->glyphOf(c);
            if(glyph && c!=' ') sprite(glyph,{float(x),float(baseline-glyph->texture.height)});
            x+=font->widthOf(c);
        }
    }
}
} // namespace d2x
