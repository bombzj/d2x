#pragma once
#include "resources/data_table.hpp"
#include <map>
#include <string>

namespace d2x {
struct ShrineDefinition {
    int code = 0, argument0 = 0, argument1 = 0;
    int durationFrames = 0, resetFrames = 0;
    std::string name, effect;
};
using ShrineCatalog = std::map<int, ShrineDefinition>;
ShrineCatalog loadShrines(const DataTable &table);
// The original initializer replaces these three retired handlers before use.
int activeShrineCode(int code);
} // namespace d2x
