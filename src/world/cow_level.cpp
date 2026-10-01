#include "cow_level.hpp"
#include "world/outdoor/outdoor_substitution.hpp"
#include <algorithm>
#include <numeric>
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
MapRecipe generateCowLevel(Archives &archives, const WorldCatalog &catalog, uint32_t seed) {
    const auto &level = catalog.level(39);
    if (level.width != 80 || level.height != 80 || level.levelType != 2)
        throw std::runtime_error("Unsupported original cow level dimensions");
    constexpr int width = 10, height = 10;
    Seed world(seed);
    Seed random(world.next() + 39);
    std::vector<OutdoorCell> cells(width * height);
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            int preset = row == 0              ? 17
                         : row == height - 1   ? 4
                         : column == 0         ? 16
                         : column == width - 1 ? 7
                                               : 0;
            if (column == 0 && row == 0)
                preset = 19;
            else if (column == width - 1 && row == 0)
                preset = 22;
            else if (column == 0 && row == height - 1)
                preset = 18;
            else if (column == width - 1 && row == height - 1)
                preset = 11;
            if (preset)
                cells[row * width + column] = {preset, random.below(catalog.presets().at(preset).files)};
        }
    for (int type = 0; type < 4; ++type)
        for (const auto &record : catalog.substitutions())
            if (record.type == type)
                applyOutdoorBorder(record, decodeDs1(archives.read(record.file)), width, height, cells,
                                   random);
    MapRecipe result;
    result.preset = 5;
    result.levelType = 2;
    result.width = level.width;
    result.height = level.height;
    result.worldX = level.offsetX;
    result.worldY = level.offsetY;
    result.ds1 = "cow-v1/39/" + std::to_string(seed);
    result.baseFloor = 0x40002;
    result.tileLibraries = catalog.terrainLibraries(2, 0x44103);
    auto append = [&](int preset, int column, int row, int variant) {
        const auto &record = catalog.presets().at(preset);
        auto source = catalog.preset(preset, 2, variant);
        result.pieces.push_back({column * 8, row * 8, record.width, record.height, preset, variant,
                                 source.ds1, source.tileLibraries, source.fillBlanks, record.populate});
    };
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            const auto &cell = cells[row * width + column];
            if (cell.preset)
                append(cell.preset, column, row, cell.variant);
            else if (cell.blank)
                result.blankAreas.push_back({column * 8, row * 8, 8, 8});
        }
    for (int preset : {50, 46, 31, 38, 39, 29, 30}) {
        const auto &record = catalog.presets().at(preset);
        int columns = record.width / 8, rows = record.height / 8;
        std::vector<int> positions((width - 2) * (height - 2));
        std::iota(positions.begin(), positions.end(), 0);
        for (size_t shuffle = 0; shuffle < positions.size(); ++shuffle) {
            int left = random.below(int(positions.size()));
            int right = random.below(int(positions.size()));
            std::swap(positions[left], positions[right]);
        }
        for (int position : positions) {
            int originX = 1 + position % (width - 2), originY = 1 + position / (width - 2);
            if (originX + columns > width || originY + rows > height)
                continue;
            bool available = true;
            for (int row = originY; row < originY + rows; ++row)
                for (int column = originX; column < originX + columns; ++column) {
                    const auto &cell = cells[row * width + column];
                    available = available && !cell.preset && !cell.blank;
                }
            if (!available)
                continue;
            append(preset, originX, originY, random.below(record.files));
            for (int row = originY; row < originY + rows; ++row)
                for (int column = originX; column < originX + columns; ++column)
                    cells[row * width + column].preset = preset;
            break;
        }
    }
    return result;
}
} // namespace d2x