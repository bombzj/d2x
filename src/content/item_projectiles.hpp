#pragma once
#include "gameplay/items/definitions.hpp"
#include "resources/archive.hpp"
#include "resources/data_table.hpp"

namespace d2x {
void loadItemProjectiles(std::vector<ItemDefinition> &items, const DataTable &missiles,
                         Archives &archives);
} // namespace d2x
