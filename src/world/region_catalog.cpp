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
    // This arrival is only known for TownN1. Other files choose a walkable point;
    // actual reciprocal warp placement belongs to the future DRLG implementation.
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
    for (const auto &[id, level] : catalog.levels()) {
        if (level.act != 0)
            continue;
        auto available = catalog.availability(
            archives, id, id == selection.level && !selection.preset ? selection.variant : 0);
        WorldEntry entry{id, level.name, available.reason, available.missing, {}};
        if (available.ready()) {
            entry.destination = RegionId(id);
            entry.status = "Preset terrain ready";
            result.regions.push_back(makeRegion(*entry.destination, level.name, *available.recipe, id == 1));
        } else if (entry.status.empty())
            entry.status = "Missing MPQ resources";
        result.entries.push_back(std::move(entry));
    }
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
