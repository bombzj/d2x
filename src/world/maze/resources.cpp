#include "world/maze.hpp"
#include <set>

namespace d2x {
int mazePresetType(int preset) {
    if (preset >= 53 && preset <= 102)
        return 3;
    if (preset >= 109 && preset <= 154)
        return 4;
    if (preset >= 167 && preset <= 205)
        return 7;
    if (preset >= 206 && preset <= 255)
        return 8;
    if (preset >= 258 && preset <= 298)
        return 10;
    if (preset >= 302 && preset <= 352)
        return 13;
    if (preset >= 354 && preset <= 357)
        return 14;
    if (preset >= 358 && preset <= 361)
        return 15;
    if (preset >= 414 && preset <= 480)
        return 17;
    if (preset >= 482 && preset <= 509)
        return 18;
    throw std::runtime_error("Unsupported maze preset family");
}
int mazePresetVariants(const WorldCatalog &catalog, int preset) {
    return preset == 167 ? 3 : preset == 359 || preset == 361 ? 4
                              : preset == 358 || preset == 360 ? 3 : catalog.presets().at(preset).files;
}
std::vector<int> mazePresets() {
    std::vector<int> ids;
    for (int id = 53; id <= 102; ++id)
        ids.push_back(id);
    for (int id = 109; id <= 154; ++id)
        ids.push_back(id);
    for (int id = 167; id <= 255; ++id)
        ids.push_back(id);
    for (int id = 258; id <= 298; ++id)
        ids.push_back(id);
    for (int id = 302; id <= 352; ++id)
        ids.push_back(id);
    for (int id = 354; id <= 361; ++id)
        ids.push_back(id);
    for (int id = 414; id <= 480; ++id)
        ids.push_back(id);
    for (int id = 482; id <= 509; ++id)
        ids.push_back(id);
    return ids;
}
std::vector<std::string> mazeMissing(Archives &archives, const WorldCatalog &catalog, int level) {
    std::set<std::string> missing;
    for (int id : mazePresets()) {
        int type = mazePresetType(id);
        if (level && catalog.level(level).levelType != type)
            continue;
        for (int v = 0; v < mazePresetVariants(catalog, id); ++v)
            for (const auto &file : catalog.missing(archives, catalog.preset(id, type, v)))
                missing.insert(file);
    }
    return {missing.begin(), missing.end()};
}
void collectMazeResources(Archives &archives, const WorldCatalog &catalog) {
    for (int id : mazePresets())
        for (int v = 0; v < mazePresetVariants(catalog, id); ++v) {
            auto recipe = catalog.preset(id, mazePresetType(id), v);
            archives.read(recipe.ds1);
            for (const auto &path : recipe.tileLibraries)
                archives.read(path);
        }
}
} // namespace d2x
