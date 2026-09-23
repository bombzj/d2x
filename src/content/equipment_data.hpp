#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/data_table.hpp"

namespace d2x {
void loadEquipmentDefinitions(std::vector<ItemDefinition> &items, const DataTable &types,
                              const std::map<std::string, DataTable, std::less<>> &tables);
} // namespace d2x
