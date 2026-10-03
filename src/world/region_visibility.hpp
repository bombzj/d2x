#pragma once
#include "world/identity.hpp"
#include "core/math.hpp"
#include <span>
#include <utility>
#include <vector>

namespace d2x {
struct Region;
// Continuous-ground display topology, separate from simulation activity.
std::vector<std::pair<int, Vec>> connectedRegionSlots(std::span<const Region> regions,
                                                      RegionId origin, bool recursive);
} // namespace d2x
