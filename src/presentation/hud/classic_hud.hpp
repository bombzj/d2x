#pragma once
#include "hud_layout.hpp"
#include <optional>

namespace d2x {
struct ClassicHudArt {
    const GpuAnimation &panel, &orbs, &overlap, &run, &points, &menu;
};
struct ClassicHudValues {
    std::optional<float> life, mana, stamina, experience; // Fractions; unknown stays empty.
    bool running{}, blueStamina{}, miniPanel{}, attributePoints{}, skillPoints{};
    std::optional<bool> pressedPoint;
};
// Shared original panel rendering. Values come from either client view; no gameplay mutations.
void drawClassicHud(const ClassicHudArt &, const ClassicHudValues &);
void drawClassicGlobe(const GpuAnimation &, const GpuAnimation &, bool mana, std::optional<float> fraction);
} // namespace d2x
