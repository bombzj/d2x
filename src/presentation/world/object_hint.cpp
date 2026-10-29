#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "core/text.hpp"
#include "core/utf8.hpp"
#include <algorithm>

namespace d2x {
namespace {
void drawFramedName(const ClassicFont &font, const PaletteBlendView &blend, const std::string &label,
                    Vec anchor, Color fillColor = BLACK, int life = 0) {
    if (label.empty() || !font.ready || font.framedLineHeight <= 0) return;
    const auto text = utf8Decode(label);
    int width = 0;
    for (const auto c : text) width += font.widthOf(c);
    const int boxWidth = width + 8;
    if (boxWidth > W) return;
    const int left = std::clamp(int(anchor.x) - boxWidth / 2, 0, W - boxWidth);
    const int bottom = std::clamp(int(anchor.y) + 2, font.framedLineHeight - 5, H - 5);
    const int fillWidth = boxWidth * std::clamp(life,0,128) / 128;
    if (fillWidth)
        blend.drawRectangle({float(left),float(bottom-font.framedLineHeight),
            float(fillWidth),float(font.framedLineHeight)},fillColor,true);
    if (fillWidth < boxWidth)
        blend.drawRectangle({float(left+fillWidth),float(bottom-font.framedLineHeight),
            float(boxWidth-fillWidth),float(font.framedLineHeight)},BLACK);
    int x = left + (boxWidth - width) / 2;
    const int baseline = bottom - font.framedBaselineOffset;
    for (const auto c : text) {
        if (const auto *glyph = font.glyphOf(c); glyph && c != ' ')
            sprite(glyph, {float(x), float(baseline - glyph->texture.height)});
        x += font.widthOf(c);
    }
}
}
void SceneView::drawCorpseLabel(const std::string &label, Vec ground) const {
    // C0450 gives player corpses red text and no life fill; C0D30 keeps
    // the same ground+8-72 anchor as living player names.
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    if (blend) drawFramedName(assets_.characterRedFont,*blend,label,ground + Vec{0,8-72});
}
void SceneView::drawInteractionLabel(const std::string &label, Vec p, int offset) const {
    // Nameless placeholder NPCs carry no label text; skip the backdrop too.
    if (label.empty()) return;
    const int width = painter_.measure(label, 16);
    DrawRectangle(int(p.x - width / 2 - 8), int(p.y - offset), width + 16, 24, {0, 0, 0, 210});
    painter_.label(label, int(p.x - width / 2), int(p.y - offset + 5), 16, WHITE);
}
void SceneView::drawNpcLabel(const std::string &label, Vec ground, int pixelHeight) const {
    // Retail C0D30 -> C0850 -> D2Win DrawFramedText: the ground anchor
    // receives +8, then MonStats2.pixHeight; the frame ends two pixels below.
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    if (blend) drawFramedName(assets_.font,*blend,label,ground + Vec{0,float(8-pixelHeight)});
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
void SceneView::drawPlayerLabel(const std::string &label, Vec ground, bool hostile, int life) const {
    const int palette = std::clamp(mapView().palette, 0, 4);
    const auto *blend = palette == 0 ? paletteBlend_.get() : actPaletteBlends_[size_t(palette)].get();
    // C0D30's player branch supplies ground+8-72 to DrawFramedTextEx.
    // The known life portion uses RGB(80,0,0)/TRANS50; the rest is TRANS75.
    if (blend) drawFramedName(hostile ? assets_.characterRedFont : assets_.font,*blend,label,
        ground + Vec{0,8-72},assets_.playerNameBackground,life);
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
    // Retail 7DC00 reads path client coordinates, without graphics offsets.
    // 7DC00 -> 7CA40 -> 7B240: the native chat font, 180px
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
