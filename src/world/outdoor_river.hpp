#pragma once
#include "outdoor_substitution.hpp"

namespace d2x {
bool placeOutdoorRiver(int width, int height, uint32_t flags, std::span<OutdoorCell> cells, Seed &seed);
} // namespace d2x