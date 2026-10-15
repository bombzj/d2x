#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"
#include <array>

namespace d2x {
struct ClassicData;
class ItemCatalog;
struct ItemInstance;
// D2Common inventory palette group and Colors.txt row; {0,0} means no shift.
std::array<int, 2> inventoryColorTransform(const ClassicData &, const ItemCatalog &, const ItemInstance &);
void loadItemAppearances(std::vector<ItemDefinition> &items,
                         const std::map<std::string, DataTable, std::less<>> &tables,
                         const std::vector<std::string> &armorTypes);
} // namespace d2x
