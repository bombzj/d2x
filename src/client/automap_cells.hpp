#pragma once
#include "contracts/automap_discovery.hpp"
#include <set>

namespace d2x {
using AutomapCells = std::array<std::set<NativeAutomapCell>, 4>;
// Preserve the first native variant in a coordinate/program group;
// return true only when a new cell was inserted.
bool insertNativeCell(std::set<NativeAutomapCell> &, NativeAutomapCell);
}
