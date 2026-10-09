#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawOrifice(Vec mouse) const {
    if(!view_.orificeObject || !view_.inventory.open) return;
    const auto *image=assets_.orificePanel.frame(0,0);if(!image) return;
    const auto bounds=orificeBounds();
    DrawTexturePro(image->texture,{0,0,float(image->texture.width),float(image->texture.height)},bounds,{0,0},0,WHITE);
    for(bool confirm:{true,false}) {
        const auto button=orificeButton(confirm);const bool pressed=CheckCollisionPointRec(rv(mouse),button) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        const auto *icons=assets_.orificeButtons.frame(0,pressed?1:0);
        if(icons) DrawTexturePro(icons->texture,{confirm?0.f:64.f,0,32,32},button,{0,0},0,confirm && !view_.orificeItem?Color{100,100,100,255}:WHITE);
    }
    if(view_.orificeItem) if(const auto *item=inventoryView_.item(view_.orificeItem->id);item && item->revision==view_.orificeItem->revision) {
        drawItemIcon(*item,orificeSlot());if(CheckCollisionPointRec(rv(mouse),orificeSlot())) drawItemTooltip(*item,{bounds.x+bounds.width,mouse.y});
    }
}
void SceneView::drawCube(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.cubeOpen || !ui.open || !inventoryView_.containers.cube) return;
    auto panel = cubeBounds();
    drawPanelFrame(false);
    for (int i = 0; i < 4; ++i) {
        const auto &tile = assets_.cubePanel.frames[i].texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                       {panel.x + (i % 2) * 256 * inventoryScale,
                        panel.y + (i / 2) * 256 * inventoryScale,
                        tile.width * inventoryScale, tile.height * inventoryScale},
                       {0, 0}, 0, WHITE);
    }
    for (const auto &grid : inventoryGrids(inventoryView_, ui))
        if (grid.container == inventoryView_.containers.cube) {
            drawContainerGrid(grid, mouse);
            if (!ui.drag && !ui.split && !ui.goldDialog)
                if (auto cell = grid.cellAt(mouse))
                    if (auto item = inventoryView_.item(inventoryView_.itemAt(grid.container, *cell)))
                        drawItemTooltip(*item, {mouse.x + 170, mouse.y});
        }
}
} // namespace d2x
