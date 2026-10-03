#include "presentation/scene_view.hpp"

namespace d2x {
void SceneView::drawBelt(Vec mouse) const {
    const auto &ui = view_.inventory;
    const auto &inventory = inventoryView_;
    auto belt = inventory.container(inventoryView_.containers.belt);
    int rows = ui.open || ui.beltExpanded ? belt->rows : 1;
    auto bounds = beltBounds(rows);
    for (int row = rows - 1; row >= 0; --row) {
        if (auto sprite = assets_.beltPanel.frame(0, 0); sprite && row > 0) {
            auto t = sprite->texture;
            DrawTexturePro(t, {0, 0, float(t.width), float(t.height)},
                           hudRect(424, 42 + row * 32, float(t.width), float(t.height)), {0, 0}, 0, WHITE);
        }
        for (int column = 0; column < 4; ++column) {
            auto box = beltSlot({column, row});
            auto item = inventory.item(inventory.itemAt(belt->id, {column, row}));
            if (item) {
                bool dragged = ui.drag && ui.drag->item.id == item->id;
                if (!dragged) drawItemIcon(*item, box);
            }
            if (CheckCollisionPointRec(rv(mouse), box))
                DrawRectangleLinesEx(box, 1, parchment);
            if (row == 0)
                painter_.label(std::to_string(column + 1), int(box.x + box.width / 2 - 3), H - 13, 10, gold);
        }
    }
    if (ui.drag && ui.drag->moved) {
        auto drop = inventoryDrop(inventoryView_, inventoryClient_, ui, mouse);
        if (beltCell(mouse, rows)) {
            Color color = drop.error == InventoryError::None ? GREEN : RED;
            DrawRectangleRec(drop.bounds, Fade(color, .18f));
            DrawRectangleLinesEx(drop.bounds, 2, color);
        }
    } else if (auto cell = beltCell(mouse, rows)) {
        if (auto item = inventory.item(inventory.itemAt(belt->id, *cell)))
            drawItemTooltip(*item, {bounds.x + bounds.width + 80, bounds.y - 5});
    }
}
} // namespace d2x
