#pragma once
#include "outdoor_grid.hpp"
#include "resources/formats.hpp"
#include <functional>

namespace d2x {
void replaceRetailSecondaryBorder(const WorldCatalog &, const SubstitutionRecord &,
                                 const MapData &, int level, uint32_t outdoorFlags,
                                 int firstPreset, RetailOutdoorGrid &,
                                 const std::function<int(uint32_t)> &decoder = {});
} // namespace d2x
