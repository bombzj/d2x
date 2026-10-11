#include "presentation/scene_view.hpp"
#include "presentation/npc/hireling_panel.hpp"
#include "skill_tooltip.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace d2x {
namespace {
int textWidth(const ClassicFont &font, std::string_view value) {
    int width=0;
    for (unsigned char character : value) width+=font.widths[character];
    return width;
}
void portraitText(const ClassicFont &font, std::string value, float x, float baseline, int maximumWidth, bool centered) {
    while (!value.empty() && textWidth(font,value)>maximumWidth) value.pop_back();
    if (centered) x=std::max(0.f,x+22-textWidth(font,value)/2.f);
    for (unsigned char character : value) {
        if (const auto *glyph=font.glyphs.frame(0,font.indices[character]);glyph && character!=' ')
            DrawTexturePro(glyph->texture,{0,0,float(glyph->texture.width),float(glyph->texture.height)},
                {(x+glyph->x)*classicPanelScale,(baseline+glyph->y-glyph->texture.height)*classicPanelScale,
                    glyph->texture.width*classicPanelScale,glyph->texture.height*classicPanelScale},{},0,WHITE);
        x+=font.widths[character];
    }
}
}
bool SceneView::hirelingPortraitVisible() const {
    if (!view_.portraitsVisible || worldViewport().x!=0 || view_.capturesWorldInput()) return false;
    const auto &portraits=hirelingView().portraits;
    return std::any_of(portraits.begin(),portraits.end(),[&](const auto &entry){
        const auto art=assets_.petPortraits.find(entry.art);
        return entry.hireling && entry.single && art!=assets_.petPortraits.end() && art->second.frame(0,0);
    });
}
void SceneView::drawHirelingPortrait(Vec mouse) const {
    if (!view_.portraitsVisible || worldViewport().x!=0 || view_.capturesWorldInput()) return;
    const int palette=std::clamp(mapView().palette,0,4);
    const auto *blend=palette==0?&paletteBlend_:actPaletteBlends_[size_t(palette)].get();
    if (!blend) return;
    size_t column=party_.portraitCount();
    float x=15+float(column)*56;
    for (const auto &entry : hirelingView().portraits) {
        const auto art=assets_.petPortraits.find(entry.art);
        const auto *image=art==assets_.petPortraits.end()?nullptr:art->second.frame(0,0);
        if (!image) continue;
        const Rectangle bounds{(x+image->x)*classicPanelScale,
            (60+image->y-image->texture.height)*classicPanelScale,
            image->texture.width*classicPanelScale,image->texture.height*classicPanelScale};
        const bool hovered=entry.hireling && entry.single==view_.hirelingPortraitHovered &&
            CheckCollisionPointRec(rv(mouse),hirelingPortraitHitBounds(partyPortraitOffset()));
        const bool hint=hovered && !view_.inventory.drag && !view_.inventory.identify &&
            view_.hirelingPortraitHoverTime>=hirelingPortraitHoverSeconds;
        if (hovered && (view_.hirelingPortraitCanFeed || hint)) blend->drawHighlight(image,bounds);
        else blend->drawHalf(image,bounds);
        if (entry.lifePercentage && *entry.lifePercentage>0) {
            const auto life=std::min<uint16_t>(*entry.lifePercentage,100);
            const float filled=std::floor(46*life/100.f)*classicPanelScale;
            const Color color=life<25?Color{255,0,0,255}:life<50?Color{192,192,0,255}:Color{0,128,0,255};
            const float left=x*classicPanelScale;
            DrawRectangleRec({left,14*classicPanelScale,filled,5*classicPanelScale},blend->paletteColor(color));
            DrawRectangleRec({left+filled,14*classicPanelScale,46*classicPanelScale-filled,5*classicPanelScale},BLACK);
        }
        if (entry.showCount) portraitText(assets_.characterLabelFont,std::to_string(entry.count),x+8,55,46,false);
        const float baseline=column%2?62:72;
        if (!entry.name.empty()) portraitText(assets_.characterLabelFont,entry.name,x,baseline,66,true);
        if (hint) drawSkillTooltip(UiPainter(assets_.font,0),{assets_.hirelingPotionHint,assets_.hirelingInventoryHint},
            {x*classicPanelScale,(baseline+36)*classicPanelScale},nullptr,{},true);
        x+=56;
        ++column;
    }
}
} // namespace d2x
