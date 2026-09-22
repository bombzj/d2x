#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"
#include <array>

namespace d2x {
struct ClassicTreasureClass {
    std::string name;
    std::vector<std::string> codes; // Keep repeated slots; never deduplicate them into an item pool.
    std::vector<int> weights;       // LoD Prob1..10; empty for the legacy NumCodes schema.
    std::optional<int> picks, noDrop, group, level;
    std::array<std::optional<int>, 4> quality{}; // Unique, Set, Rare, Magic.
};
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
