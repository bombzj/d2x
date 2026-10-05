#include "outdoor.hpp"
#include "world/generation_seed.hpp"
#include "world/map.hpp"
#include "outdoor_paths.hpp"
#include "world/native_map.hpp"
#include <algorithm>
#include <set>

// Rectangular Act I outdoor branch adapted from D2MOO DrlgOutdoors/OutWild/OutPlace, MIT.
// Copyright (c) 2020-2025 The Phrozen Keep community. See docs/licenses/D2MOO.txt.
namespace d2x {
namespace {
void alignPresetBoundary(Archives &archives, const WorldCatalog &catalog,
                         std::map<int, OutdoorPosition> &layout, int id, int preset, uint32_t worldSeed) {
    auto &p = layout.at(id);
    auto recipe = catalog.preset(preset, catalog.level(id).levelType, id == 1 || id == 40 ? p.direction : 0);
    auto &boundary = p.boundaries.front();
    auto contact = boundary;
    const int size = boundary.side % 2 ? p.height : p.width;
    contact.start = std::clamp(boundary.contactStart, 0, size);
    contact.end = std::clamp(boundary.contactEnd, 0, size);
    recipe.width = p.width;
    recipe.height = p.height;
    recipe.boundaries = {contact};
    TileLibraryCache cache(archives);
    Map map;
    Seed tileRoot(worldSeed);
    map.load(archives, cache, recipe, tileRoot.next() + uint32_t(id));
    const Vec origin = id == 1 || id == 40 ? map.actSpawn() : map.spawn;
    const auto reachable = map.grid.reachableFrom(origin);
    int first = contact.end * 5, last = -1;
    for (int lateral = contact.start * 5; lateral < contact.end * 5; ++lateral) {
        const int column = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width * 5 - 1 : lateral;
        const int row = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height * 5 - 1 : lateral;
        if (map.grid.walkable(column, row) && reachable[size_t(row * map.grid.width + column)]) {
            first = std::min(first, lateral);
            last = lateral;
        }
    }
    if (last < 0)
        throw std::runtime_error("Native preset has no reachable connection on its linked edge: " + recipe.ds1);
    boundary.start = first / 5;
    boundary.end = last / 5 + 1;
    if (id == 1) {
        // The original town DS1 supplies the road crossing. Only consider
        // floors on the reachable gate, not decorative dirt elsewhere on the edge.
        auto findPath = [&](bool includeAuthoredApproach) {
        for (int tile = boundary.start; tile < boundary.end; ++tile) {
            const int x = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width : tile;
            const int y = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height : tile;
            bool passage = false;
            for (int sub = tile * 5; sub < (tile + 1) * 5; ++sub) {
                const int column = boundary.side == 1 ? 0 : boundary.side == 3 ? p.width * 5 - 1 : sub;
                const int row = boundary.side == 2 ? 0 : boundary.side == 0 ? p.height * 5 - 1 : sub;
                passage |= map.grid.walkable(column, row) && reachable[size_t(row) * map.grid.width + column];
            }
            if (!passage) continue;
            const bool road = std::any_of(map.terrain.data.floors.begin(), map.terrain.data.floors.end(),
                [&](const auto &layer) {
                    const auto &cell = layer[size_t(y) * map.terrain.data.width + x];
                    // TownE's reachable opening is an authored bridge approach,
                    // not a dirt tile. Preserve it and join the outdoor dirt at its edge.
                    return isOutdoorPathFloor(cell) || (includeAuthoredApproach && cell.present() && cell.key() != 0);
                });
            if (!road) continue;
            if (boundary.pathStart < 0) boundary.pathStart = tile;
            if (tile > boundary.pathStart + 1) break;
            boundary.pathEnd = tile + 1;
        }
        };
        findPath(false);
        if (boundary.pathStart < 0) findPath(true);
        if (boundary.pathStart < 0)
            throw std::runtime_error("Native town gate has no authored road approach: " + recipe.ds1 +
                " side=" + std::to_string(boundary.side) + " span=" + std::to_string(boundary.start) +
                ":" + std::to_string(boundary.end));
    }
    auto &other = layout.at(boundary.destination);
    for (auto &back : other.boundaries)
        if (back.destination == id) {
            const int offset = boundary.side % 2 ? p.y - other.y : p.x - other.x;
            back.start = boundary.start + offset;
            back.end = boundary.end + offset;
            if (boundary.pathStart >= 0) {
                back.pathStart = boundary.pathStart + offset;
                back.pathEnd = boundary.pathEnd + offset;
            }
        }
}
} // namespace
std::map<int, MapRecipe> generateAct1Outdoors(Archives &archives, const WorldCatalog &catalog,
                                              uint32_t seed) {
    TileLibraryCache cache(archives);
    NativeMapGenerator generator(archives, catalog, cache, 0, seed);
    std::map<int, MapRecipe> result;
    for (const auto &[id, level] : catalog.levels())
        if (level.act == 0 && (level.generation == GenerationKind::Outdoor || id == 1 || id == 26))
            result.emplace(id, generator.recipe(id));
    return result;
}

std::map<int, MapRecipe> generateAct2Outdoors(Archives &archives, const WorldCatalog &catalog, uint32_t seed) {
    auto layout = layoutAct2(catalog, seed);
    alignPresetBoundary(archives, catalog, layout, 40, 301, seed);
    std::map<int, MapRecipe> result;
    for (const auto &[id, position] : layout) {
        auto recipe = id == 40 ? catalog.preset(301, 12, position.direction)
                               : generateDesert(archives, catalog, position, seed);
        recipe.width = position.width;
        recipe.height = position.height;
        recipe.worldX = position.x;
        recipe.worldY = position.y;
        recipe.boundaries = position.boundaries;
        result.emplace(id, std::move(recipe));
    }
    return result;
}
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
