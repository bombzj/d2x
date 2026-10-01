#include "outdoor_shrines.hpp"
#include "resources/formats.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Adapted from D2MOO DrlgOutdoors::SpawnAct12Shrines and DrlgTileSub, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
void placeAct1OutdoorShrines(Archives &archives, const WorldCatalog &catalog, const LevelRecord &level,
                             std::span<int> occupied, MapRecipe &recipe, Seed &seed) {
    // The native Act I wilderness rule marks five eligible macro cells, cycling
    // through the first four rows of the LvlSub type named by Levels.SubShrine.
    if (level.id < 2 || level.id > 7)
        return;
    std::vector<const SubstitutionRecord *> shrineRecords;
    for (const auto &record : catalog.substitutions())
        if (record.type == level.shrineSubstitution)
            shrineRecords.push_back(&record);
    if (level.shrineSubstitution < 0 || shrineRecords.size() < 4)
        throw std::runtime_error("Missing original outdoor shrine rows for level " + std::to_string(level.id));
    const int width = recipe.width / 8, height = recipe.height / 8;
    if (width < 3 || height < 3 || occupied.size() != size_t(width) * height)
        throw std::runtime_error("Invalid outdoor shrine grid");

    std::array<MapData, 4> patterns;
    for (int index = 0; index < 4; ++index) {
        const auto &record = *shrineRecords[size_t(index)];
        if (record.gridSize != 1 || record.file.empty())
            throw std::runtime_error("Invalid original outdoor shrine row for level " + std::to_string(level.id));
        patterns[index] = decodeDs1(archives.read(record.file));
        if (patterns[index].substitutionGroups.empty())
            throw std::runtime_error("Original outdoor shrine has no substitution groups: " + record.file);
    }

    std::vector<int> cells(size_t(width - 2) * (height - 2));
    std::iota(cells.begin(), cells.end(), 0);
    int shrine = seed.below(4);
    for (size_t index = 0; index < cells.size(); ++index) {
        int left = seed.below(int(cells.size()));
        int right = seed.below(int(cells.size()));
        std::swap(cells[size_t(left)], cells[size_t(right)]);
    }
    int placed = 0;
    for (int cell : cells) {
        if (placed == 5)
            break;
        const int column = cell % (width - 2) + 1, row = cell / (width - 2) + 1;
        const int index = row * width + column;
        if (occupied[size_t(index)])
            continue;
        const auto &record = *shrineRecords[size_t(shrine)];
        const auto &groups = patterns[shrine].substitutionGroups;
        const int groupIndex = seed.below(int(groups.size()));
        const auto &group = groups[size_t(groupIndex)];
        if (group.variants || group.width > 7 || group.height > 7)
            throw std::runtime_error("Unsupported original outdoor shrine group: " + record.file);
        MapPiece piece;
        piece.x = column * 8 + seed.below(8 - group.width) + 1;
        piece.y = row * 8 + seed.below(8 - group.height) + 1;
        piece.width = group.width;
        piece.height = group.height;
        piece.ds1 = record.file;
        piece.tileLibraries = recipe.tileLibraries;
        auto libraries = catalog.terrainLibraries(level.levelType, record.dt1Mask);
        piece.tileLibraries.insert(piece.tileLibraries.end(), libraries.begin(), libraries.end());
        piece.populate = false;
        piece.substitutionGroup = groupIndex;
        recipe.pieces.push_back(std::move(piece));
        occupied[size_t(index)] = -1;
        shrine = (shrine + 1) % 4;
        ++placed;
    }
}
} // namespace d2x
