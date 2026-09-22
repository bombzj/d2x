#pragma once
#include "content/world_catalog.hpp"
#include "generation_seed.hpp"
#include "resources/formats.hpp"
#include <span>

namespace d2x {
struct OutdoorCell {
    int preset = 0, variant = -1;
    bool blank = false, levelLink = false;
};
void applyOutdoorBorder(const SubstitutionRecord &record, const MapData &pattern, int width, int height,
                        std::span<OutdoorCell> cells, Seed &seed);
} // namespace d2x