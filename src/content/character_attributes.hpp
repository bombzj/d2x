#pragma once
#include "gameplay/character/attributes.hpp"
#include "resources/data_table.hpp"
#include <vector>

namespace d2x {
std::vector<CharacterDefinition> loadCharacterDefinitions(const DataTable &characters);
} // namespace d2x
