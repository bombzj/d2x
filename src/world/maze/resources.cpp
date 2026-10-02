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
    if (preset >= 510 && preset <= 528)
        return 19;
    if (preset >= 659 && preset <= 664)
        return 23;
    if (preset >= 652 && preset <= 658)
        return 22;
    if (preset >= 530 && preset <= 604)
        return 21;
    if (preset >= 605 && preset <= 651)
        return 22;
    if (preset >= 665 && preset <= 702)
        return 24;
    if (preset >= 705 && preset <= 746)
        return 25;
    if (preset >= 754 && preset <= 795)
        return 22;
    if (preset >= 1003 && preset <= 1041)
        return 33;
    if (preset >= 1042 && preset <= 1052)
        return 32;
    if (preset >= 1059 && preset <= 1085)
        return 34;
    if (preset >= 836 && preset <= 862)
        return 28;
    if (preset >= 798 && preset <= 835)
        return 27;
    if (preset >= 865 && preset <= 879)
        return 30;
    if (preset >= 880 && preset <= 1002)
        return 31;
    if (preset >= 1053 && preset <= 1058)
        return 35;
    throw std::runtime_error("Unsupported maze preset family");
}
int mazePresetVariants(const WorldCatalog &catalog, int preset) {
        if (preset == 573 || preset == 574 || preset == 629 || preset == 630 || preset == 646 || preset == 647)
            return 2;
        if (preset >= 575 && preset <= 604) return 3;
        if (preset == 631) return 1;
        return preset == 524 ? 5 : preset >= 510 && preset <= 524 && catalog.presets().at(preset).files == 0 ? 4
            : preset == 167 ? 3 : preset == 359 || preset == 361 ? 4
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
    for (int id = 510; id <= 528; ++id)
        ids.push_back(id);
    for (int id = 659; id <= 702; ++id)
        ids.push_back(id);
    for (int id = 652; id <= 658; ++id)
        ids.push_back(id);
    for (int id = 530; id <= 651; ++id)
        ids.push_back(id);
    for (int id = 705; id <= 746; ++id)
        ids.push_back(id);
    for (int id = 754; id <= 795; ++id)
        ids.push_back(id);
    for (int id = 1003; id <= 1052; ++id)
        ids.push_back(id);
    for (int id = 1059; id <= 1085; ++id)
        ids.push_back(id);
    for (int id = 836; id <= 862; ++id)
        ids.push_back(id);
    for (int id = 798; id <= 835; ++id)
        ids.push_back(id);
    for (int id = 865; id <= 879; ++id)
        ids.push_back(id);
    for (int id = 880; id <= 1002; ++id)
        if (id != 967 && id != 979) ids.push_back(id);
    for (int id = 1053; id <= 1058; ++id)
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
            if (!catalog.presets().at(id).variants[size_t(v)].empty())
            for (const auto &file : catalog.missing(archives, catalog.preset(id, type, v)))
                missing.insert(file);
    }
    return {missing.begin(), missing.end()};
}
void collectMazeResources(Archives &archives, const WorldCatalog &catalog) {
    for (int id : mazePresets())
        for (int v = 0; v < mazePresetVariants(catalog, id); ++v) {
            if (catalog.presets().at(id).variants[size_t(v)].empty()) continue;
            auto recipe = catalog.preset(id, mazePresetType(id), v);
            archives.read(recipe.ds1);
            for (const auto &path : recipe.tileLibraries)
                archives.read(path);
        }
}
} // namespace d2x
