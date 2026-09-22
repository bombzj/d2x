#pragma once
#include "gameplay/items/definitions.hpp"
#include "gameplay/items/state.hpp"
#include "primitives.hpp"
#include <optional>

namespace d2x {
// Shared geometry for bag, storage and belt. It knows no input gestures or item rules.
struct ContainerGrid {
    EntityId container;
    Vec origin, stride;
    int columns, rows;
    float cellSize;
    Rectangle cellBounds(Cell cell, int width = 1, int height = 1) const {
        return {origin.x + cell.x * stride.x, origin.y + cell.y * stride.y, width * cellSize,
                height * cellSize};
    }
    Rectangle itemBounds(Cell cell, const ItemDefinition &definition) const {
        return cellBounds(cell, definition.width, definition.height);
    }
    std::optional<Cell> cellAt(Vec mouse) const {
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x)
                if (CheckCollisionPointRec(rv(mouse), cellBounds({x, y})))
                    return Cell{x, y};
        return std::nullopt;
    }
};
} // namespace d2x
