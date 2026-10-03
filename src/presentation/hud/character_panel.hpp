#pragma once
#include "gameplay/character/allocation.hpp"
#include "classic_panel.hpp"
#include "presentation/graphics/primitives.hpp"
#include <optional>

namespace d2x {
inline Rectangle characterBounds() { return classicPanelBounds(false); }
inline Rectangle characterArtRect(float x, float y, float width, float height) {
    auto panel = characterBounds();
        return {panel.x + x * classicPanelScale, panel.y + y * classicPanelScale,
            width * classicPanelScale, height * classicPanelScale};
}
inline Rectangle characterClose() { return characterArtRect(128, 389, 32, 34); }
inline Rectangle characterAddButton(int index) {
    constexpr float y[] = {76, 140, 223, 288};
    return characterArtRect(122, y[index], 28, 28);
}
inline std::optional<Attribute> characterAttributeAt(Vec mouse) {
    for (int index = 0; index < 4; ++index)
        if (CheckCollisionPointRec(rv(mouse), characterAddButton(index)))
            return Attribute(index);
    return std::nullopt;
}
} // namespace d2x
