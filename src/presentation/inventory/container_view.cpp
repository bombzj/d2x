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
        DrawRectangleRec({box.x + 1, box.y + 1, box.width - 2, box.height - 2},
                         !ui.drag && (id == hovered || id == ui.selected) ? Color{83, 71, 37, 130}
                                                                         : Color{30, 36, 26, 110});
    }
    const auto drop = inventoryDrop(inventoryView_, inventoryClient_, ui, mouse, view_.hirelingOpen);
    drawInventoryDrop(drop, grid.cellBounds({}, grid.columns, grid.rows));
    for (auto id : contents) {
        const auto &item = *inventory.item(id);
        if (ui.hidesItem(item) || (view_.orificeItem && view_.orificeItem->id==id)) continue;
        auto box = grid.itemBounds(std::get<ContainerLocation>(item.location).cell,
                                   *inventory.definition(item.definition));
        drawItemIcon(item, box);
        if (!ui.drag && (id == hovered || id == ui.selected))
            DrawRectangleLinesEx(box, 1, id == hovered ? parchment : gold);
        const auto *definition = inventory.definition(item.definition);
        if (definition->maxStack > 1 || definition->bookCapacity) {
            auto quantity = std::to_string(definition->bookCapacity ? item.charges : item.quantity);
            int width = painter_.measure(quantity, 12);
            DrawRectangle(int(box.x + box.width) - width - 6, int(box.y + box.height) - 17, width + 4, 15,
                          {0, 0, 0, 210});
            painter_.label(quantity, int(box.x + box.width) - width - 4, int(box.y + box.height) - 16, 12,
                           !item.quantity ? RED : WHITE);
        }
    }
}
} // namespace d2x
