#pragma once
#include "gameplay/loot/special.hpp"
#include "gameplay/items/quality.hpp"
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace d2x {
struct GemRecord {
    std::array<std::vector<PropertyRange>, 3> properties;
};
struct RunewordRecord {
    int row = -1, stringId = 0;
    bool complete = false, server = false;
    std::string name;
    std::vector<std::string> runes, includedTypes, excludedTypes;
    std::vector<PropertyRange> properties;
};
} // namespace d2x
