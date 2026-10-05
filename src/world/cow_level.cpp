#include "cow_level.hpp"
#include <algorithm>
#include <set>

namespace d2x {
std::vector<MapRecipe> cowLevelTemplates(const WorldCatalog &catalog) {
    std::vector<MapRecipe> result;
    for (int preset = 4; preset <= 50; ++preset) {
        if (preset >= 24 && preset <= 28)
            continue;
        if (preset >= 32 && preset <= 45 && preset != 38 && preset != 39)
            continue;
        if (preset >= 47 && preset <= 49)
            continue;
        const auto &record = catalog.presets().at(preset);
        for (int variant = 0; variant < record.files; ++variant)
            result.push_back(catalog.preset(preset, 2, variant));
    }
    return result;
}
std::vector<std::string> cowLevelMissing(Archives &archives, const WorldCatalog &catalog) {
    std::set<std::string> missing;
    for (const auto &recipe : cowLevelTemplates(catalog))
        for (const auto &path : catalog.missing(archives, recipe))
            missing.insert(path);
    for (int type = 0; type < 4; ++type) {
        bool found = false;
        for (const auto &record : catalog.substitutions())
            if (record.type == type) {
                found = true;
                if (!archives.contains(record.file))
                    missing.insert(record.file);
            }
        if (!found)
            missing.insert("LvlSub border type " + std::to_string(type));
    }
    for (const auto &path : catalog.terrainLibraries(2, 0x44103))
        if (!archives.contains(path))
            missing.insert(path);
    return {missing.begin(), missing.end()};
}
void collectCowLevelResources(Archives &archives, const WorldCatalog &catalog) {
    for (const auto &recipe : cowLevelTemplates(catalog)) {
        archives.read(recipe.ds1);
        for (const auto &path : recipe.tileLibraries)
            archives.read(path);
    }
    for (const auto &record : catalog.substitutions())
        if (record.type >= 0 && record.type < 4)
            archives.read(record.file);
}
} // namespace d2x
