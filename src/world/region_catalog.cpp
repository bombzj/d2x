#include "cow_level.hpp"
#include "world/outdoor/outdoor.hpp"
#include "region.hpp"
#include "world/generation_seed.hpp"
#include <algorithm>

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
    auto missingOutdoor = outdoorMissing(archives, catalog);
    auto outdoors = missingOutdoor.empty() ? generateAct1Outdoors(archives, catalog, selection.seed)
                                           : std::map<int, MapRecipe>{};
    auto deserts = generateAct2Outdoors(archives, catalog, selection.seed);
    outdoors.insert(deserts.begin(), deserts.end());
    auto jungles = generateAct3Jungles(catalog, selection.seed);
    outdoors.insert(jungles.begin(), jungles.end());
    auto mesas = generateAct4Outdoors(catalog, selection.seed);
    outdoors.insert(mesas.begin(), mesas.end());
    auto barricades = generateAct5Barricades(archives, catalog, selection.seed);
    outdoors.insert(barricades.begin(), barricades.end());
    if (catalog.levels().contains(134)) {
        const auto &level = catalog.level(134);
        OutdoorPosition position{level.id, level.offsetX, level.offsetY, level.width, level.height, 0, {}};
        outdoors.emplace(134, generateDesert(archives, catalog, position, selection.seed));
    }
    for (const auto &[id, level] : catalog.levels()) {
        if (level.act < 0 || level.act > 4)
            continue;
        int variant = id == selection.level && !selection.preset ? selection.variant : 0;
        if (level.generation == GenerationKind::Preset && id != selection.level && !outdoors.contains(id)) {
            Seed world(selection.seed);
            Seed random(world.next() + uint32_t(id));
            for (const auto &[presetId, preset] : catalog.presets())
                if (preset.level == id) {
                    variant = random.below(preset.files);
                    break;
                }
        }
        auto available = catalog.availability(archives, id, id == 40 ? 1 : variant);
        WorldEntry entry{id, level.name, available.reason, available.missing, {}};
        if (outdoors.contains(id)) {
            entry.destination = RegionId(id);
            entry.status = "Connected outdoor terrain";
            entry.missing.clear();
            result.regions.push_back(makeRegion(*entry.destination, level.name, outdoors.at(id), level.town));
        } else if (id == 82 || id == 83) {
            MapRecipe recipe;
            recipe.act = level.act;
            recipe.levelType = level.levelType;
            recipe.preset = id == 82 ? 652 : 653;
            recipe.width = level.width;
            recipe.height = level.height;
            recipe.ds1 = "kurast-fixed-v1/" + std::to_string(id) + "/" + std::to_string(selection.seed);
            struct Placement { int preset, x, y; };
            constexpr Placement layout[]{{653,0,0},{654,16,0},{655,48,0},
                {656,0,32},{657,16,32},{658,48,32}};
            auto place = [&](int presetId, int column, int row) {
                const auto &preset = catalog.presets().at(presetId);
                auto source = catalog.preset(presetId, level.levelType, 0);
                recipe.pieces.push_back({column, row, preset.width, preset.height, presetId, 0,
                    source.ds1, source.tileLibraries, source.fillBlanks, preset.populate});
            };
            if (id == 82) place(652, 0, 0);
            else for (const auto &piece : layout) place(piece.preset, piece.x, piece.y);
            entry.destination = RegionId(id);
            entry.status = "Original Kurast fixed terrain";
            entry.missing.clear();
            result.regions.push_back(makeRegion(*entry.destination, level.name, std::move(recipe)));
        } else if (id == 108) {
            MapRecipe recipe;
            recipe.act = level.act;
            recipe.levelType = level.levelType;
            recipe.preset = 857;
            recipe.width = recipe.height = 120;
            recipe.worldX = level.offsetX;
            recipe.worldY = level.offsetY;
            recipe.ds1 = "chaos-v1/" + std::to_string(selection.seed);
            constexpr int layout[]{836,836,836,836,836, 836,836,861,836,836,
                836,858,862,859,836, 836,836,860,836,836, 836,836,857,836,836};
            Seed world(selection.seed);
            Seed random(world.next() + uint32_t(id));
            for (int index = 0; index < 25; ++index) {
                const auto &preset = catalog.presets().at(layout[index]);
                const int variant = random.below(preset.files);
                auto source = catalog.preset(preset.id, level.levelType, variant);
                recipe.pieces.push_back({(index % 5) * 24, (index / 5) * 24, 24, 24,
                    preset.id, variant, source.ds1, source.tileLibraries, source.fillBlanks, preset.populate});
            }
            entry.destination = RegionId(id);
            entry.status = "Original Chaos Sanctuary room layout";
            entry.missing.clear();
            result.regions.push_back(makeRegion(*entry.destination, level.name, std::move(recipe)));
        } else if (id == 110) {
            MapRecipe recipe;
            recipe.act = level.act;
            recipe.levelType = level.levelType;
            recipe.preset = 865;
            recipe.width = level.width;
            recipe.height = level.height;
            recipe.worldX = level.offsetX;
            recipe.worldY = level.offsetY;
            recipe.ds1 = "siege-v1/" + std::to_string(selection.seed);
            int column = recipe.width;
            for (int index = 0; index < 15; ++index) {
                const auto &preset = catalog.presets().at(865 + index);
                column -= preset.width;
                if (column < 0 || preset.height != recipe.height)
                    throw std::runtime_error("Original siege strip dimensions do not match Levels");
                auto source = catalog.preset(preset.id, level.levelType, 0);
                recipe.pieces.push_back({column, 0, preset.width, preset.height, preset.id, 0,
                    source.ds1, source.tileLibraries, source.fillBlanks, preset.populate});
            }
            entry.destination = RegionId(id);
            entry.status = "Original Bloody Foothills strips";
            entry.missing.clear();
            result.regions.push_back(makeRegion(*entry.destination, level.name, std::move(recipe)));
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
                int entranceDirection = 0;
                if (id == 28) {
                    auto court = std::find_if(result.regions.begin(), result.regions.end(),
                        [](const auto &region) { return int(region.definition.id) == 27; });
                    if (court != result.regions.end())
                        entranceDirection = court->recipe.variant;
                }
                result.regions.push_back(makeRegion(
                    *entry.destination, level.name,
                    generateMaze(catalog, id, selection.seed, selection.difficulty, entranceDirection)));
            }
        } else if (available.ready()) {
            entry.destination = RegionId(id);
            entry.status = "Preset terrain ready";
            result.regions.push_back(makeRegion(*entry.destination, level.name, *available.recipe, level.town));
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
    auto river = std::find_if(result.regions.begin(), result.regions.end(),
        [](const auto &region) { return int(region.definition.id) == 107; });
    auto sanctuary = std::find_if(result.regions.begin(), result.regions.end(),
        [](const auto &region) { return int(region.definition.id) == 108; });
    if (river != result.regions.end() && sanctuary != result.regions.end()) {
        auto &lava = river->recipe;
        auto &chaos = sanctuary->recipe;
        auto bridge = lava.pieces.end();
        for (auto piece = lava.pieces.begin(); piece != lava.pieces.end(); ++piece) {
            lava.width = std::max(lava.width, piece->x + piece->width);
            lava.height = std::max(lava.height, piece->y + piece->height);
            if (piece->preset == 856 && (bridge == lava.pieces.end() || piece->y < bridge->y))
                bridge = piece;
        }
        if (bridge == lava.pieces.end()) throw std::runtime_error("Missing original Chaos bridge");
        lava.worldX = chaos.worldX + 48 - bridge->x;
        lava.worldY = chaos.worldY + chaos.height - bridge->y;
        lava.boundaries.push_back({108, 2, bridge->x, bridge->x + bridge->width,
            bridge->x, bridge->x + bridge->width, bridge->y});
        chaos.boundaries.push_back({107, 0, 48, 72, 48, 72, chaos.height});
    }
    auto harrogath = std::find_if(result.regions.begin(), result.regions.end(),
        [](const auto &region) { return int(region.definition.id) == 109; });
    auto siege = std::find_if(result.regions.begin(), result.regions.end(),
        [](const auto &region) { return int(region.definition.id) == 110; });
    if (harrogath != result.regions.end() && siege != result.regions.end()) {
        auto &town = harrogath->recipe;
        const auto &identity = catalog.level(109);
        town.worldX = identity.offsetX;
        town.worldY = identity.offsetY;
        const auto terrain = decodeDs1(archives.read(town.ds1));
        town.width = terrain.width - 1;
        town.height = terrain.height - 1;
        auto &battlefield = siege->recipe;
        const int start = std::max(town.worldY, battlefield.worldY);
        const int end = std::min(town.worldY + town.height, battlefield.worldY + battlefield.height);
        town.boundaries.push_back({110, 1, start - town.worldY, end - town.worldY,
            start - town.worldY, end - town.worldY});
        battlefield.boundaries.push_back({109, 3, start - battlefield.worldY, end - battlefield.worldY,
            start - battlefield.worldY, end - battlefield.worldY});
        auto highlands = std::find_if(result.regions.begin(), result.regions.end(),
            [](const auto &region) { return int(region.definition.id) == 111; });
        if (highlands != result.regions.end()) {
            const auto &border = highlands->recipe.boundaries.back();
            battlefield.boundaries.push_back({111,1,
                highlands->recipe.worldY + border.start - battlefield.worldY,
                highlands->recipe.worldY + border.end - battlefield.worldY,
                highlands->recipe.worldY + border.start - battlefield.worldY,
                highlands->recipe.worldY + border.end - battlefield.worldY});
        }
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
            throw std::runtime_error("The selected level is not supported");
        if (!entry->destination)
            throw std::runtime_error("Level " + std::to_string(selection.level) +
                                     " unavailable: " + failure(*entry));
        result.start = *entry->destination;
    }
    return result;
}
} // namespace d2x
