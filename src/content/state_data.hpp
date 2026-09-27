#pragma once
#include "gameplay/effects/state.hpp"
#include "resources/data_table.hpp"
#include <map>
#include <string>

namespace d2x {
struct CombatStateRecord {
    CombatStateDefinition definition;
    std::string overlay;
};
using CombatStateCatalog = std::map<std::string, CombatStateRecord, std::less<>>;
CombatStateCatalog loadCombatStates(const DataTable &states);
} // namespace d2x
