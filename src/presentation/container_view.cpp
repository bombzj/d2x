#include "scene_view.hpp"

namespace d2x {
void SceneView::drawContainerGrid(const ContainerGrid &grid, Vec mouse) const {
    const auto &inventory = session_.inventory();
    const auto &ui = view_.inventory;
    auto hover = grid.cellAt(mouse);
    EntityId hovered = hover ? inventory.itemAt(grid.container, *hover) : EntityId{};
    for (auto id : inventory.contents(grid.container)) {
        const auto &item = *inventory.item(id);
        auto box = grid.itemBounds(std::get<ContainerLocation>(item.location).cell,
                                   *inventory.catalog().find(item.definition));
        bool dragged = ui.drag && ui.drag->item.id == id && ui.drag->moved;
        DrawRectangleRec({box.x + 1, box.y + 1, box.width - 2, box.height - 2},
                         id == hovered || id == ui.selected ? Color{83, 71, 37, 130}
                                                            : Color{30, 36, 26, 110});
        drawItemIcon(item, box, dragged ? Fade(WHITE, .3f) : WHITE);
        if (id == hovered || id == ui.selected)
            DrawRectangleLinesEx(box, 1, id == hovered ? parchment : gold);
        const auto *definition = inventory.catalog().find(item.definition);
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
