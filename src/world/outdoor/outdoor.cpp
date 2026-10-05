#include "outdoor.hpp"
#include <algorithm>
#include <set>

// Rectangular Act I outdoor branch adapted from D2MOO DrlgOutdoors/OutWild/OutPlace, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
std::vector<MapRecipe> outdoorTemplates(const WorldCatalog &catalog) {
    std::vector<MapRecipe> result;
    for (int id = 2; id <= 163; ++id) {
        if (!(id <= 52 || id == 108 || id >= 160) || id == 40)
            continue;
        const auto &preset = catalog.presets().at(id);
        for (int v = 0; v < 6; ++v)
            if (!preset.variants[v].empty())
                result.push_back(catalog.preset(id, 2, v));
    }
    return result;
}
std::vector<std::string> outdoorMissing(Archives &archives, const WorldCatalog &catalog) {
    std::set<std::string> result;
    for (const auto &r : outdoorTemplates(catalog))
        for (const auto &path : catalog.missing(archives, r))
            result.insert(path);
    for (const auto &path : catalog.terrainLibraries(2, 0x44103))
        if (!archives.contains(path))
            result.insert(path);
    for (int type = 0; type <= 5; ++type) {
        bool found = false;
        for (const auto &record : catalog.substitutions())
            if (record.type == type) {
                found = true;
                if (!archives.contains(record.file))
                    result.insert(record.file);
            }
        if (!found)
            result.insert("LvlSub type " + std::to_string(type));
    }
    for (int id = 2; id <= 7; ++id) {
        const int type = catalog.level(id).shrineSubstitution;
        if (type < 0 || std::count_if(catalog.substitutions().begin(), catalog.substitutions().end(),
                                      [&](const auto &record) { return record.type == type; }) < 4)
            result.insert("Levels.SubShrine / LvlSub rows for level " + std::to_string(id));
    }
    return {result.begin(), result.end()};
}
} // namespace d2x
