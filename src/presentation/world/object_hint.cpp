#include "presentation/world/palette_blend_view.hpp"
#include "presentation/scene_view.hpp"
#include "core/text.hpp"
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
} // namespace d2x
