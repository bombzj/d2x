#pragma once
#include "contracts/map.hpp"
#include "presentation/graphics/primitives.hpp"
#include <span>

namespace d2x {
struct WaypointPanelArt {
    const GpuAnimation &border, &panel, &tabs, &icons, &close;
    const std::array<ClassicFont, 3> &fonts;
    std::string title;
};
struct WaypointPanelHit {
    std::optional<int> act;
    std::optional<size_t> row;
    bool close{};
};
WaypointPanelHit waypointPanelHit(Vec mouse, size_t rows);
void drawWaypointPanel(const WaypointPanelArt &, std::span<const TravelEntryView>,
    const std::array<bool, 5> &acts, int selectedAct, int currentLevel, Vec mouse);
void loadWaypointFonts(Graphics &, Archives &, const ClassicFont &, std::array<ClassicFont, 3> &);
void drawClassicPanelFrame(const GpuAnimation &, bool right);
} // namespace d2x
