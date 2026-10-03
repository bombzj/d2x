#pragma once
#include "gameplay/effects/definition.hpp"
#include "resources/data_table.hpp"
#include <map>
#include <string>
#include <string_view>

namespace d2x {
struct CombatStateRecord {
    CombatStateDefinition definition;
    std::string overlay;
    std::string secondaryOverlay;
};
using CombatStateCatalog = std::map<std::string, CombatStateRecord, std::less<>>;
CombatStateCatalog loadCombatStates(const DataTable &states);
std::string_view shrineStateName(int code);
} // namespace d2x
