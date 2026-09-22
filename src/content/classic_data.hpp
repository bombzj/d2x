#pragma once
#include "gameplay/items/definitions.hpp"
#include "gameplay/loot/loot.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <array>

namespace d2x {
using ClassicTreasureClass = TreasureClass;
struct ClassicMonsterData {
    std::string name, token;
    std::array<std::array<std::optional<unsigned>, 4>, 3> treasureClasses{};
};
// Version-specific MPQ adapter. Rules consume typed records, not archive handles or TXT cells.
struct ClassicData {
    ItemCatalog items;
    std::map<std::string, DataTable, std::less<>> tables;
    std::vector<ClassicTreasureClass> treasures;
    std::vector<ClassicMonsterData> monsters;
    std::string profile;
};
ClassicData loadClassicData(Archives &archives);
void loadLodTreasureData(ClassicData &data);
} // namespace d2x
