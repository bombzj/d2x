#include "world/plan.hpp"
#include "world/native_map.hpp"
#include <algorithm>
#include <array>
#include <memory>

namespace d2x {
namespace {
RegionPlan makeRegion(RegionId id, std::string name, MapRecipe recipe, bool town = false) {
    RegionDefinition definition;
    definition.id = id;
    definition.name = std::move(name);
    definition.mapPath = recipe.ds1;
    definition.safe = town;
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
            if (preset.level <= 0 || catalog.level(preset.level).generation != GenerationKind::Preset)
                continue;
            for (int variant = 0; variant < 6; ++variant)
                if (normalize(selection.map) == preset.variants[variant]) {
                    selection.level = preset.level;
                    selection.variant = variant;
                    found = true;
                }
        }
        if (!found)
            throw std::runtime_error("--map requires a complete LvlPrest entry; use --preset and "
                                     "--level-type for room previews");
    }
    WorldPlan result;
    TileLibraryCache nativeLibraries(archives);
    std::array<std::unique_ptr<NativeMapGenerator>, 5> generators;
    for (int act = 0; act < int(generators.size()); ++act)
        generators[size_t(act)] = std::make_unique<NativeMapGenerator>(
            archives, catalog, nativeLibraries, act, selection.seed, selection.difficulty);
    for (const auto &[id, level] : catalog.levels()) {
        if (level.act < 0 || level.act >= int(generators.size())) continue;
        auto recipe = generators[size_t(level.act)]->recipe(id);
        if (!selection.map.empty() && id == selection.level) {
            const auto available = catalog.availability(archives, id, selection.variant);
            if (!available.ready()) throw std::runtime_error(available.reason);
            recipe = *available.recipe;
        }
        result.regions.push_back(makeRegion(RegionId(id), level.name, std::move(recipe), level.town));
        result.entries.push_back({id, level.name, "Shared native terrain", {}, RegionId(id)});
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
    if (selection.preset)
        result.start = preview(selection.preset, selection.levelType, selection.variant);
    else {
        auto entry = std::find_if(result.entries.begin(), result.entries.end(),
                                  [&](const auto &e) { return e.level == selection.level; });
        if (entry == result.entries.end())
            throw std::runtime_error("The selected level is not supported");
        if (!entry->destination)
            throw std::runtime_error("Level " + std::to_string(selection.level) +
                                     " unavailable: " + failure(*entry));
        result.start = *entry->destination;
    }
    return result;
}
} // namespace d2x
