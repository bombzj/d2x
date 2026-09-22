#pragma once
#include "content/world_catalog.hpp"

namespace d2x {
// Immutable generation plan. Geometry, archive decoding and runtime state remain separate.
inline constexpr uint32_t defaultMapSeed = 210;
std::vector<int> mazePresets();
bool supportsMaze(int level);
MapRecipe generateMaze(const WorldCatalog &catalog, int level, uint32_t seed, int difficulty);
std::vector<std::string> mazeMissing(Archives &archives, const WorldCatalog &catalog);
void collectMazeResources(Archives &archives, const WorldCatalog &catalog);
} // namespace d2x
