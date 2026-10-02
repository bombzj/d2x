#include "resources/archive.hpp"
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
constexpr std::array<std::string_view, 36> levelNames{
    "None", "1 Town", "1 Wilderness", "1 Cave", "1 Crypt", "1 Monestary", "1 Courtyard",
    "1 Barracks", "1 Jail", "1 Cathedral", "1 Catacombs", "1 Tristram", "2 Town", "2 Sewer",
    "2 Harem", "2 Basement", "2 Desert", "2 Tomb", "2 Lair", "2 Arcane", "3 Town", "3 Jungle",
    "3 Kurast", "3 Spider", "3 Dungeon", "3 Sewer", "4 Town", "4 Mesa", "4 Lava", "5 Town",
    "5 Siege", "5 Barricade", "5 Temple", "5 Ice", "5 Baal", "5 Lava"};
} // namespace

AutomapCatalog::AutomapCatalog(Archives &archives) {
    DataTable types(archives.read("data/global/excel/lvltypes.txt"));
    DataTable automap(archives.read("data/global/excel/automap.txt"));
    DataTable objects(archives.read("data/global/excel/objects.txt"));
    DataTable monsters(archives.read("data/global/excel/monstats.txt"));
    DataTable monsterModes(archives.read("data/global/excel/monstats2.txt"));
    for (auto column : {"Name", "Id"})
        if (!types.has(column)) throw std::runtime_error("LvlTypes lacks automap level identity");
    for (auto column : {"LevelName", "TileName", "Style", "StartSequence",
                        "EndSequence", "Cel1", "Cel2", "Cel3", "Cel4"})
        if (!automap.has(column)) throw std::runtime_error("Automap.txt lacks original cell rules");
    if (!objects.has("Id") || !objects.has("AutoMap"))
        throw std::runtime_error("Objects.txt lacks original automap markers");
    std::map<int, std::string_view> namesByType;
    for (size_t row = 0; row < types.rows().size(); ++row)
        if (auto id = types.number(row, "Id"))
            if (*id >= 0 && size_t(*id) < levelNames.size())
                namesByType[*id] = levelNames[size_t(*id)];
    for (size_t row = 0; row < automap.rows().size(); ++row) {
        if (automap.value(row, "LevelName").empty() || automap.value(row, "TileName").empty())
            continue;
        Rule rule;
        rule.style = automap.number(row, "Style").value_or(-1);
        rule.first = automap.number(row, "StartSequence").value_or(-1);
        rule.last = automap.number(row, "EndSequence").value_or(-1);
        for (int i = 0; i < 4; ++i)
            rule.cels[i] = automap.number(row, "Cel" + std::to_string(i + 1)).value_or(-1);
        for (const auto &[levelType, levelName] : namesByType)
            if (levelName == automap.value(row, "LevelName"))
                for (int tileType = 0; tileType < int(tileNames.size()); ++tileType)
                    if (tileNames[tileType] == automap.value(row, "TileName"))
                        rules_[{levelType, tileType}].push_back(rule);
    }
    for (size_t row = 0; row < objects.rows().size(); ++row)
        if (auto id = objects.number(row, "Id"))
            if (auto cel = objects.number(row, "AutoMap"); cel && *cel > 0)
                objectCels_[*id] = *cel;
    std::map<std::string, int, std::less<>> monsterCels;
    for (size_t row = 0; row < monsterModes.rows().size(); ++row)
        if (auto cel = monsterModes.number(row, "automapCel"); cel && *cel > 0)
            monsterCels[std::string(monsterModes.value(row, "Id"))] = *cel;
    for (size_t row = 0; row < monsters.rows().size(); ++row) {
        auto cel = monsterCels.find(monsters.value(row, "MonStatsEx"));
        if (cel != monsterCels.end())
            npcCels_[std::string(monsters.value(row, "Id"))] = cel->second;
        else if (monsters.number(row, "interact").value_or(0) &&
                 !monsters.number(row, "isAtt").value_or(0))
            npcCels_[std::string(monsters.value(row, "Id"))] = 317;
    }
}

int AutomapCatalog::tileCel(int levelType, const MapCell &cell, int x, int y) const {
    if (levelType == 0) return -1;
    const int style = int((cell.value >> 20) & 63);
    const int sequence = int((cell.value >> 8) & 255);
    auto group = rules_.find({levelType, cell.orientation});
    if (group == rules_.end()) return -1;
    for (const auto &rule : group->second) {
        if ((rule.style != -1 && rule.style != style) ||
            (rule.first != -1 && (sequence < rule.first || sequence > rule.last))) continue;
        int count = 0;
        while (count < 4 && rule.cels[count] >= 0) ++count;
        if (!count) return -1;
        // Retail uses a shared automap RNG; coordinate selection keeps these visual
        // variants stable across redraws without changing gameplay random state.
        unsigned choice = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^
                          unsigned(style) * 83492791u ^ unsigned(sequence);
        return rule.cels[choice % unsigned(count)];
    }
    return -1;
}

int AutomapCatalog::objectCel(int objectClass) const {
    auto found = objectCels_.find(objectClass);
    return found == objectCels_.end() ? -1 : found->second;
}
int AutomapCatalog::npcCel(std::string_view monsterClass) const {
    auto found = npcCels_.find(monsterClass);
    return found == npcCels_.end() ? -1 : found->second;
}
} // namespace d2x
