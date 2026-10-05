#pragma once
#include "outdoor_grid.hpp"
#include "resources/formats.hpp"

namespace d2x {
void replaceRetailSecondaryBorder(const WorldCatalog &, const SubstitutionRecord &,
                                 const MapData &, int level, uint32_t outdoorFlags,
                                 int firstPreset, RetailOutdoorGrid &);
} // namespace d2x
