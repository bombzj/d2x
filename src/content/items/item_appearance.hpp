#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"

namespace d2x {
void loadItemAppearances(std::vector<ItemDefinition> &items,
                         const std::map<std::string, DataTable, std::less<>> &tables,
                         const std::vector<std::string> &armorTypes);
} // namespace d2x
