#include "world_catalog.hpp"
#include "resources/data_table.hpp"
#include <charconv>
#include <set>
#include <stdexcept>

namespace d2x {
namespace {
std::string member(std::string_view value) {
    return value.empty() || value == "0" ? std::string{}
                                         : normalize("data/global/tiles/" + std::string(value));
}
uint32_t mask(const DataTable &table, size_t row) {
    auto value = table.value(row, "Dt1Mask");
    if (value.empty())
        return 0;
    uint32_t result = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size())
        throw std::runtime_error("Invalid LvlPrest/LvlSub Dt1Mask");
    return result;
}
template <class T> void insert(std::map<int, T> &records, int id, T record) {
    if (!records.emplace(id, std::move(record)).second)
        throw std::runtime_error("Duplicate MPQ world record: " + std::to_string(id));
}
} // namespace
WorldCatalog::WorldCatalog(Archives &archives) {
    for (const auto *name : {"levels", "lvlprest", "lvltypes", "lvlmaze", "lvlsub", "lvlwarp"}) {
        DataTable table(archives.read(std::string("data/global/excel/") + name + ".txt"));
        const std::map<std::string_view, std::vector<std::string_view>> required = {
            {"levels",
             {"Id", "Act", "DrlgType", "LevelType", "SizeX", "SizeY", "LevelName", "Vis0", "Warp0"}},
            {"lvlprest", {"Def", "LevelId", "File1", "File6", "Dt1Mask", "FillBlanks"}},
            {"lvltypes", {"Id", "File 1", "File 32"}},
            {"lvlmaze", {"Level", "Rooms", "SizeX", "SizeY", "Merge"}},
            {"lvlsub", {"Type", "File", "Dt1Mask", "GridSize", "Prob0"}},
            {"lvlwarp", {"Id", "SelectX", "SelectDX", "ExitWalkX"}}};
        for (const auto column : required.at(name))
            if (!table.has(column))
                throw std::runtime_error(std::string("Unsupported world table schema: ") + name + " lacks " +
                                         std::string(column));
        for (size_t row = 0; row < table.rows().size(); ++row) {
            // Retail tables contain an "Expansion" separator with empty ID cells.
            // It is not another record zero and must not change native IDs.
            std::string_view idColumn = std::string_view(name) == "lvlprest"  ? "Def"
                                        : std::string_view(name) == "lvlmaze" ? "Level"
                                        : std::string_view(name) == "lvlsub"  ? "Type"
                                                                              : "Id";
            if (table.value(row, idColumn).empty())
                continue;
            auto number = [&](std::string_view key, int fallback = 0) {
                return table.number(row, key).value_or(fallback);
            };
            std::string_view kind = name;
            if (kind == "levels") {
                LevelRecord record;
                record.id = number("Id");
                record.act = number("Act");
                record.levelType = number("LevelType");
                record.generation = GenerationKind(number("DrlgType"));
                record.name = table.value(row, "LevelName");
                record.width = number("SizeX");
                record.height = number("SizeY");
                record.offsetX = number("OffsetX");
                record.offsetY = number("OffsetY");
                record.depend = number("Depend");
                record.subtype = number("SubType", -1);
                record.theme = number("SubTheme", -1);
                record.waypoint = number("Waypoint", -1);
                record.shrineSubstitution = number("SubShrine", -1);
                auto &population = record.population;
                population.supported = table.has("mon1") && table.has("MonDen(N)");
                population.types = number("NumMon");
                population.rangedFirst = number("rangedspawn") != 0;
                population.warpDistanceSquared = number("WarpDist");
                const std::array<std::string, 3> suffixes{"", "(N)", "(H)"};
                for (int i = 0; i < 3; ++i) {
                    population.density[i] = number("MonDen" + suffixes[i]);
                    population.uniqueMin[i] = number("MonUMin" + suffixes[i]);
                    population.uniqueMax[i] = number("MonUMax" + suffixes[i]);
                    population.level[i] = number("MonLvl" + std::to_string(i + 1) + "Ex");
                }
                for (int i = 1; population.supported && i <= 10; ++i) {
                    for (auto [prefix, list] :
                         {std::pair{"mon", &population.normal}, std::pair{"nmon", &population.nightmareHell},
                          std::pair{"umon", &population.unique}}) {
                        auto code = table.value(row, std::string(prefix) + std::to_string(i));
                        if (!code.empty())
                            list->emplace_back(code);
                    }
                }
                for (int i = 0; population.supported && i < 4; ++i) {
                    auto suffix = std::to_string(i + 1);
                    population.critters[i] = table.value(row, "cmon" + suffix);
                    population.critterChance[i] = number("cpct" + suffix);
                    population.critterAmount[i] = number("camt" + suffix);
                }
                for (int i = 0; i < 8; ++i) {
                    auto suffix = std::to_string(i);
                    record.visible[i] = number("Vis" + suffix);
                    record.warps[i] = number("Warp" + suffix, -1);
                    record.objectGroups[i] = number("ObjGrp" + suffix);
                    record.objectProbabilities[i] = number("ObjPrb" + suffix);
                }
                if (record.id)
                    insert(levels_, record.id, std::move(record));
            } else if (kind == "lvlprest") {
                PresetRecord record;
                record.id = number("Def");
                if (table.value(row, "LevelId") == "#REF!") {
                    record.level = -1;
                    diagnostics_.push_back("lvlprest Def " + std::to_string(record.id) +
                                           " has #REF! LevelId; no level binding");
                } else
                    record.level = number("LevelId");
                record.name = table.value(row, "Name");
                record.width = number("SizeX");
                record.height = number("SizeY");
                record.files = number("Files");
                record.fillBlanks = number("FillBlanks") != 0;
                record.killEdge = number("KillEdge") != 0;
                record.populate = number("Populate") != 0;
                record.dt1Mask = mask(table, row);
                for (int i = 0; i < 6; ++i)
                    record.variants[i] = member(table.value(row, "File" + std::to_string(i + 1)));
                if (record.id)
                    insert(presets_, record.id, std::move(record));
            } else if (kind == "lvltypes") {
                std::array<std::string, 32> files;
                for (int i = 0; i < 32; ++i)
                    files[i] = member(table.value(row, "File " + std::to_string(i + 1)));
                insert(types_, number("Id"), std::move(files));
            } else if (kind == "lvlmaze") {
                // The shipped demo contains an unused spreadsheet #REF! row. Preserve a diagnostic,
                // never coerce it into level zero or silently assign it to another level.
                if (table.value(row, "Level") == "#REF!") {
                    diagnostics_.push_back("lvlmaze row " + std::to_string(row) +
                                           " has #REF! Level; ignored");
                    continue;
                }
                MazeRecord record{number("Level"), number("Rooms"), number("SizeX"), number("SizeY"),
                                  number("Merge")};
                record.difficultyRooms = {record.rooms, number("Rooms(N)", record.rooms),
                                          number("Rooms(H)", record.rooms)};
                if (record.level)
                    insert(mazes_, record.level, record);
            } else if (kind == "lvlsub") {
                SubstitutionRecord record;
                record.type = number("Type");
                record.name = table.value(row, "Name");
                record.file = member(table.value(row, "File"));
                record.gridSize = number("GridSize");
                record.borderType = number("BordType");
                record.dt1Mask = mask(table, row);
                for (int i = 0; i < 5; ++i) {
                    auto suffix = std::to_string(i);
                    record.probability[i] = number("Prob" + suffix);
                    record.trials[i] = number("Trials" + suffix);
                    record.maximum[i] = number("Max" + suffix);
                }
                substitutions_.push_back(std::move(record));
            } else {
                WarpRecord record;
                record.id = number("Id");
                record.name = table.value(row, "Name");
                record.selectX = number("SelectX");
                record.selectY = number("SelectY");
                record.selectWidth = number("SelectDX");
                record.selectHeight = number("SelectDY");
                record.exitX = number("ExitWalkX");
                record.exitY = number("ExitWalkY");
                record.offsetX = number("OffsetX");
                record.offsetY = number("OffsetY");
                record.direction = table.value(row, "Direction");
                auto &directions = warps_[record.id];
                for (const auto &existing : directions)
                    if (existing.direction == record.direction)
                        throw std::runtime_error("Duplicate LvlWarp ID/direction");
                directions.push_back(std::move(record));
            }
        }
    }
    for (const auto &[id, level] : levels_) {
        if (level.act != 0)
            continue;
        if (!types_.contains(level.levelType))
            throw std::runtime_error("Unknown Act I LevelType");
        for (int i = 0; i < 8; ++i) {
            if (level.visible[i] && !levels_.contains(level.visible[i]))
                throw std::runtime_error("Unknown Vis level");
            if (level.warps[i] >= 0 && !warps_.contains(level.warps[i]))
                throw std::runtime_error("Unknown Warp ID");
        }
    }
}
const LevelRecord &WorldCatalog::level(int id) const {
    auto found = levels_.find(id);
    if (found == levels_.end())
        throw std::runtime_error("Unknown MPQ level ID: " + std::to_string(id));
    return found->second;
}
std::vector<std::string> WorldCatalog::typeLibraries(int levelType) const {
    std::vector<std::string> result;
    for (const auto &file : types_.at(levelType))
        if (!file.empty())
            result.push_back(file);
    return result;
}
MapRecipe WorldCatalog::preset(int id, int levelType, int variant) const {
    if (!presets_.contains(id))
        throw std::runtime_error("Unknown LvlPrest Def: " + std::to_string(id));
    if (!types_.contains(levelType))
        throw std::runtime_error("Unknown LevelType: " + std::to_string(levelType));
    const auto &record = presets_.at(id);
    if (variant < 0 || variant >= 6 || record.variants[variant].empty())
        throw std::runtime_error("This LvlPrest variant has no DS1 file");
    MapRecipe recipe;
    recipe.preset = id;
    recipe.variant = variant;
    recipe.levelType = levelType;
    recipe.ds1 = record.variants[variant];
    recipe.fillBlanks = record.fillBlanks;
    recipe.tileLibraries = terrainLibraries(levelType, record.dt1Mask);
    return recipe;
}
std::vector<std::string> WorldCatalog::terrainLibraries(int levelType, uint32_t mask) const {
    std::vector<std::string> result;
    const auto &files = types_.at(levelType);
    for (int i = 0; i < 32; ++i)
        if ((mask & (uint32_t(1) << i)) && !files[i].empty())
            result.push_back(files[i]);
    for (auto file : {"act1/outdoors/blank.dt1", "act1/barracks/inviswal.dt1", "act1/barracks/warp.dt1"})
        result.push_back(member(file));
    return result;
}
std::vector<std::string> WorldCatalog::missing(Archives &archives, const MapRecipe &recipe) const {
    std::vector<std::string> result;
    for (const auto &file : recipe.tileLibraries)
        if (!archives.contains(file))
            result.push_back(file);
    if (!archives.contains(recipe.ds1))
        result.push_back(recipe.ds1);
    return result;
}
LevelAvailability WorldCatalog::availability(Archives &archives, int levelId, int variant) const {
    const auto &record = level(levelId);
    LevelAvailability result;
    if (record.generation == GenerationKind::Preset) {
        const PresetRecord *selected = nullptr;
        for (const auto &[id, preset] : presets_)
            if (preset.level == levelId) {
                if (selected) {
                    result.reason = "Multiple level presets require a generation rule";
                    return result;
                }
                selected = &preset;
            }
        if (!selected)
            result.reason = "No LvlPrest record for this level";
        else {
            result.recipe = preset(selected->id, record.levelType, variant);
            result.missing = missing(archives, *result.recipe);
        }
    } else {
        result.reason = record.generation == GenerationKind::Maze
                            ? "Original maze generation not implemented"
                            : "Original outdoor generation not implemented";
        for (const auto &file : typeLibraries(record.levelType))
            if (!archives.contains(file))
                result.missing.push_back(file);
    }
    return result;
}
} // namespace d2x
