#include "scene_view.hpp"

namespace d2x {
void SceneView::drawCube(Vec mouse) const {
    const auto &ui = view_.inventory;
    if (!ui.cubeOpen || !ui.open || !session_.playerContainers().cube) return;
    auto panel = cubeBounds();
    DrawRectangle(int(panel.x) - 5, 0, int(panel.width) + 10, H - HUD, {0, 0, 0, 150});
    for (int i = 0; i < 4; ++i) {
        const auto &tile = assets_.cubePanel.frames[i].texture;
        DrawTexturePro(tile, {0, 0, float(tile.width), float(tile.height)},
                       {panel.x + (i % 2) * 256 * inventoryScale,
                        panel.y + (i / 2) * 256 * inventoryScale,
                        tile.width * inventoryScale, tile.height * inventoryScale},
                       {0, 0}, 0, WHITE);
    }
    for (const auto &grid : inventoryGrids(session_, ui))
        if (grid.container == session_.playerContainers().cube) {
            drawContainerGrid(grid, mouse);
            if (!ui.drag && !ui.split && !ui.goldDialog)
                if (auto cell = grid.cellAt(mouse))
                    if (auto item = session_.inventory().item(session_.inventory().itemAt(grid.container, *cell)))
                        drawItemTooltip(*item, {inventoryBounds().x - 12, mouse.y});
        }
}
} // namespace d2x
