#pragma once
#include "inventory_panel.hpp"
#include "primitives.hpp"

namespace d2x {
inline Rectangle questBounds() { return {16, 20, 400, 540}; }
inline Rectangle questArtRect(float x, float y, float width, float height) {
    const auto panel = questBounds();
    return {panel.x + x * inventoryScale, panel.y + y * inventoryScale,
            width * inventoryScale, height * inventoryScale};
}
inline Rectangle questIconBounds(int index) {
    return questArtRect(52.f + float(index % 2) * 145.f,
                        72.f + float(index / 2) * 105.f, 72, 85);
}
inline Rectangle questCloseBounds() { return questArtRect(274, 386, 34, 35); }
inline Rectangle questBackBounds() { return questArtRect(22, 386, 65, 35); }
} // namespace d2x
