#include "cow_level.hpp"
#include "outdoor.hpp"
#include "region.hpp"
#include <algorithm>

namespace d2x {
namespace {
RegionPlan makeRegion(RegionId id, std::string name, MapRecipe recipe, bool town = false) {
    RegionDefinition definition;
    definition.id = id;
    definition.name = std::move(name);
    definition.mapPath = recipe.ds1;
    definition.safe = town;
    // TownN1 has a known town-centre arrival. Other variants use inspectionArrival;
    // travel uses reciprocal boundary/warp arrivals instead.
    if (town && recipe.variant == 0) {
        definition.arrival = {140.5f, 64.5f};
        definition.customArrival = true;
    }
    return {std::move(definition), std::move(recipe)};
}
std::string failure(const WorldEntry &entry) {
    std::string result = entry.status;
    if (!entry.missing.empty())
        result += ": " + entry.missing.front();
    return result;
}
} // namespace
WorldPlan planWorld(Archives &archives, const WorldCatalog &catalog, WorldSelection selection) {
    // Retain --map only for catalogued complete presets. A pathname cannot define
    // LevelType, Dt1Mask or whether a file is a complete level.
    if (!selection.map.empty()) {
        bool found = false;
        for (const auto &[id, preset] : catalog.presets()) {
            if (preset.level <= 0 || catalog.level(preset.level).act != 0)
                continue;
            for (int variant = 0; variant < 6; ++variant)
                if (normalize(selection.map) == preset.variants[variant]) {
                    selection.level = preset.level;
                    selection.variant = variant;
                    found = true;
                }
        }
        if (!found)
            throw std::runtime_error("--map requires a complete Act I LvlPrest entry; use --preset and "
                                     "--level-type for room previews");
    }
    WorldPlan result;
    auto missingOutdoor = outdoorMissing(archives, catalog);
    auto outdoors = missingOutdoor.empty() ? generateAct1Outdoors(archives, catalog, selection.seed)
                                           : std::map<int, MapRecipe>{};
    for (const auto &[id, level] : catalog.levels()) {
        if (level.act != 0)
            continue;
        auto available = catalog.availability(
            archives, id, id == selection.level && !selection.preset ? selection.variant : 0);
        WorldEntry entry{id, level.name, available.reason, available.missing, {}};
        if (outdoors.contains(id)) {
            entry.destination = RegionId(id);
            entry.status = "Connected outdoor terrain";
            entry.missing.clear();
            result.regions.push_back(makeRegion(*entry.destination, level.name, outdoors.at(id), id == 1));
        } else if (id == 39) {
            entry.missing = cowLevelMissing(archives, catalog);
            entry.status = entry.missing.empty() ? "Generated cow terrain / quest portal unavailable"
                                                 : "Missing original cow level resources";
            if (entry.missing.empty()) {
                entry.destination = RegionId(id);
                result.regions.push_back(makeRegion(*entry.destination, level.name,
                                                    generateCowLevel(archives, catalog, selection.seed)));
            }
        } else if (supportsMaze(id)) {
            auto missingMaze = mazeMissing(archives, catalog, id);
            entry.missing = missingMaze;
            entry.status =
                missingMaze.empty() ? "Generated maze / linked stairs" : "Missing original maze resources";
            if (missingMaze.empty()) {
                entry.destination = RegionId(id);
                result.regions.push_back(makeRegion(
                    *entry.destination, level.name,
                    generateMaze(catalog, id, selection.seed, selection.difficulty,
                                 selection.level == 27 && !selection.preset ? selection.variant : 0)));
            }
        } else if (available.ready()) {
            entry.destination = RegionId(id);
            entry.status = "Preset terrain ready";
            result.regions.push_back(makeRegion(*entry.destination, level.name, *available.recipe, id == 1));
        } else if (entry.status.empty())
            entry.status = "Missing MPQ resources";
        result.entries.push_back(std::move(entry));
    }
    auto court = std::find_if(result.regions.begin(), result.regions.end(),
                              [](const auto &region) { return int(region.definition.id) == 27; });
    auto barracks = std::find_if(result.regions.begin(), result.regions.end(),
                                 [](const auto &region) { return int(region.definition.id) == 28; });
    if (court != result.regions.end()) {
        auto terrain = decodeDs1(archives.read(court->recipe.ds1));
        auto &recipe = court->recipe;
        recipe.width = terrain.width - 1;
        recipe.height = terrain.height - 1;
    }
    for (int dependentId : {27, 33}) {
        const auto &level = catalog.level(dependentId);
        int parentId = dependentId == 27 ? 26 : 32;
        auto dependent = std::find_if(result.regions.begin(), result.regions.end(), [&](const auto &region) {
            return int(region.definition.id) == dependentId;
        });
        auto parent = std::find_if(result.regions.begin(), result.regions.end(),
                                   [&](const auto &region) { return int(region.definition.id) == parentId; });
        if (dependent == result.regions.end() || parent == result.regions.end())
            continue;
        for (auto *recipe : {&dependent->recipe, &parent->recipe})
            if (!recipe->width || !recipe->height) {
                auto terrain = decodeDs1(archives.read(recipe->ds1));
                recipe->width = terrain.width - 1;
                recipe->height = terrain.height - 1;
            }
        auto &child = dependent->recipe;
        auto &base = parent->recipe;
        if (level.depend != parentId || level.offsetY + child.height != 0)
            throw std::runtime_error("Unsupported dependent preset geometry");
        child.worldX = base.worldX + level.offsetX;
        child.worldY = base.worldY + level.offsetY;
        int start = std::max(child.worldX, base.worldX);
        int end = std::min(child.worldX + child.width, base.worldX + base.width);
        if (start >= end)
            throw std::runtime_error("Dependent presets have no shared boundary");
        child.boundaries.push_back({parentId, 0, start - child.worldX, end - child.worldX});
        base.boundaries.push_back({dependentId, 2, start - base.worldX, end - base.worldX});
    }
    if (court != result.regions.end() && barracks != result.regions.end())
        connectBarracks(court->recipe, barracks->recipe, court->recipe.width, court->recipe.height);
    auto preview = [&](int id, int type, int variant) {
        auto recipe = catalog.preset(id, type, variant);
        auto missing = catalog.missing(archives, recipe);
        if (!missing.empty())
            throw std::runtime_error("Preset missing MPQ resource: " + missing.front());
        auto regionId = RegionId(10000 + id);
        auto name = catalog.presets().at(id).name + " [TEMPLATE]";
        result.regions.push_back(makeRegion(regionId, name, std::move(recipe)));
        result.entries.push_back({0, name, "Original template; not a complete level", {}, regionId});
        return regionId;
    };
    // Existing scenes remain accessible, explicitly labelled as template previews.
    for (const auto &[id, type] : {std::pair{50, 2}, {108, 2}, {55, 3}})
        if (selection.preset != id && catalog.missing(archives, catalog.preset(id, type)).empty())
            preview(id, type, 0);
    if (selection.preset)
        result.start = preview(selection.preset, selection.levelType, selection.variant);
    else {
        auto entry = std::find_if(result.entries.begin(), result.entries.end(),
                                  [&](const auto &e) { return e.level == selection.level; });
        if (entry == result.entries.end())
            throw std::runtime_error("Only Act I levels are supported");
        if (!entry->destination)
            throw std::runtime_error("Level " + std::to_string(selection.level) +
                                     " unavailable: " + failure(*entry));
        result.start = *entry->destination;
    }
    return result;
}
} // namespace d2x
