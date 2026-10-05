#pragma once
#include "world/outdoor/native_act_layout.hpp"
#include "outdoor_grid.hpp"
#include <span>

namespace d2x {
struct RetailBoundaryVertex {
    int x{}, y{}, direction{};
    uint32_t flags{};
};
std::vector<RetailBoundaryVertex> buildRetailBoundary(const NativeActLayout &, int level);
uint32_t markRetailAct1Cliffs(int level, std::span<RetailBoundaryVertex>);
void markRetailBoundaryLinks(const NativeActLayout &, int level,
                             std::span<const RetailBoundaryVertex>, RetailOutdoorGrid &);
} // namespace d2x
