#pragma once
// World seeds are selected at the application boundary.
#include "content/world_catalog.hpp"

namespace d2x {
// Immutable generation plan. Geometry, archive decoding and runtime state remain separate.
std::vector<int> mazePresets();
int mazePresetType(int preset);
int mazePresetVariants(const WorldCatalog &catalog, int preset);
bool supportsMaze(int level);
MapRecipe generateMaze(const WorldCatalog &catalog, int level, uint32_t seed, int difficulty,
                       int entranceDirection = 0);
std::vector<std::string> mazeMissing(Archives &archives, const WorldCatalog &catalog, int level = 0);
void collectMazeResources(Archives &archives, const WorldCatalog &catalog);
void connectBarracks(MapRecipe &court, MapRecipe &barracks, int courtWidth, int courtHeight);
} // namespace d2x
