#include "world/maze.hpp"
#include <set>

namespace d2x {
std::vector<int> mazePresets() {
    std::vector<int> ids;
    for (int id = 53; id <= 102; ++id)
        ids.push_back(id);
    for (int id = 109; id <= 154; ++id)
        ids.push_back(id);
    return ids;
}
std::vector<std::string> mazeMissing(Archives &archives, const WorldCatalog &catalog) {
    std::set<std::string> missing;
    for (int id : mazePresets())
        for (int v = 0; v < catalog.presets().at(id).files; ++v)
            for (const auto &file : catalog.missing(archives, catalog.preset(id, id < 108 ? 3 : 4, v)))
                missing.insert(file);
    return {missing.begin(), missing.end()};
}
void collectMazeResources(Archives &archives, const WorldCatalog &catalog) {
    for (int id : mazePresets())
        for (int v = 0; v < catalog.presets().at(id).files; ++v) {
            auto recipe = catalog.preset(id, id < 108 ? 3 : 4, v);
            archives.read(recipe.ds1);
            for (const auto &path : recipe.tileLibraries)
                archives.read(path);
        }
}
} // namespace d2x
