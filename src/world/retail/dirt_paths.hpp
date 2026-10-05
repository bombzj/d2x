#pragma once
#include "world/outdoor/native_act_layout.hpp"
#include "outdoor_grid.hpp"

namespace d2x {
struct RetailPathPoint { int x{}, y{}; };
using RetailDirtPaths = std::vector<std::vector<RetailPathPoint>>;
RetailDirtPaths buildRetailDirtPaths(const NativeActLayout &, int level, uint32_t outdoorFlags,
                                   RetailOutdoorGrid &);
} // namespace d2x
