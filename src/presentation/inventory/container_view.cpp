#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawContainerGrid(const ContainerGrid &grid, Vec mouse) const {
    const auto &inventory = inventoryView_;
    const auto &ui = view_.inventory;
    auto hover = grid.cellAt(mouse);
    EntityId hovered = hover ? inventory.itemAt(grid.container, *hover) : EntityId{};
    const auto contents = inventory.contents(grid.container);
    // Paint cell backgrounds and the drop footprint before any item pixels.
    for (auto id : contents) {
        const auto &item = *inventory.item(id);
        auto box = grid.itemBounds(std::get<ContainerLocation>(item.location).cell,
                                   *inventory.definition(item.definition));
        bool dragged = ui.hidesItem(item);
        if (dragged || (view_.orificeItem && view_.orificeItem->id==id)) continue;
        drawItemBackground(item, box, !ui.drag && id == hovered);
    }
    const auto drop = inventoryDropAt(mouse);
    drawInventoryDrop(drop, grid.cellBounds({}, grid.columns, grid.rows));
    for (auto id : contents) {
        const auto &item = *inventory.item(id);
        if (ui.hidesItem(item) || (view_.orificeItem && view_.orificeItem->id==id)) continue;
        auto box = grid.itemBounds(std::get<ContainerLocation>(item.location).cell,
                                   *inventory.definition(item.definition));
        drawItemIcon(item, box, !ui.drag && id == hovered);
    }
}
} // namespace d2x
