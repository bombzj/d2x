#include "automap_data.hpp"
#include "resources/data_table.hpp"
#include <stdexcept>
#include <string_view>

namespace d2x {
namespace {
// D2CMP tile orientations and Automap.txt TileName, as in d2moo D2CMP.h.
constexpr std::array<std::string_view, 20> tileNames{
    "fl", "wl", "wr", "wtlr", "wtll", "wtr", "wbl", "wbr", "wld", "wrd",
    "wle", "wre", "co", "sh", "tr", "rf", "ld", "rd", "fd", "fi"};
std::string automapLevelName(std::string_view name) {
    if (!name.starts_with("Act ")) return {};
    auto separator = name.find(" - ");
    if (separator == std::string_view::npos) return {};
    return std::string(name.substr(4, separator - 4)) + " " +
           std::string(name.substr(separator + 3));
}
} // namespace

AutomapCatalog::AutomapCatalog(Archives &archives) {
    DataTable types(archives.read("data/global/excel/lvltypes.txt"));
    DataTable automap(archives.read("data/global/excel/automap.txt"));
    DataTable objects(archives.read("data/global/excel/objects.txt"));
    for (auto column : {"Name", "Id"})
        if (!types.has(column)) throw std::runtime_error("LvlTypes lacks automap level identity");
    for (auto column : {"LevelName", "TileName", "Style", "StartSequence",
                        "EndSequence", "Cel1", "Cel2", "Cel3", "Cel4"})
        if (!automap.has(column)) throw std::runtime_error("Automap.txt lacks original cell rules");
    if (!objects.has("Id") || !objects.has("AutoMap"))
        throw std::runtime_error("Objects.txt lacks original automap markers");
    std::map<int, std::string> levelNames;
    for (size_t row = 0; row < types.rows().size(); ++row)
        if (auto id = types.number(row, "Id"))
            levelNames[*id] = automapLevelName(types.value(row, "Name"));
    for (size_t row = 0; row < automap.rows().size(); ++row) {
        if (automap.value(row, "LevelName").empty() || automap.value(row, "TileName").empty())
            continue;
        Rule rule;
        rule.style = automap.number(row, "Style").value_or(-1);
        rule.first = automap.number(row, "StartSequence").value_or(-1);
        rule.last = automap.number(row, "EndSequence").value_or(-1);
        for (int i = 0; i < 4; ++i)
            rule.cels[i] = automap.number(row, "Cel" + std::to_string(i + 1)).value_or(-1);
        for (const auto &[levelType, levelName] : levelNames)
            if (levelName == automap.value(row, "LevelName"))
                for (int tileType = 0; tileType < int(tileNames.size()); ++tileType)
                    if (tileNames[tileType] == automap.value(row, "TileName"))
                        rules_[{levelType, tileType}].push_back(rule);
    }
    for (size_t row = 0; row < objects.rows().size(); ++row)
        if (auto id = objects.number(row, "Id"))
            if (auto cel = objects.number(row, "AutoMap"); cel && *cel > 0)
                objectCels_[*id] = *cel;
}

int AutomapCatalog::tileCel(int levelType, const Tile &tile, int x, int y) const {
    auto group = rules_.find({levelType, tile.orientation});
    if (group == rules_.end()) return -1;
    for (const auto &rule : group->second) {
        if ((rule.style != -1 && rule.style != tile.main) ||
            (rule.first != -1 && (tile.sub < rule.first || tile.sub > rule.last))) continue;
        int count = 0;
        while (count < 4 && rule.cels[count] >= 0) ++count;
        if (!count) return -1;
        // Retail uses a shared automap RNG; coordinate selection keeps these visual
        // variants stable across redraws without changing gameplay random state.
        unsigned choice = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^
                          unsigned(tile.main) * 83492791u ^ unsigned(tile.sub);
        return rule.cels[choice % unsigned(count)];
    }
    return -1;
}

int AutomapCatalog::objectCel(int objectClass) const {
    auto found = objectCels_.find(objectClass);
    return found == objectCels_.end() ? -1 : found->second;
}
} // namespace d2x
