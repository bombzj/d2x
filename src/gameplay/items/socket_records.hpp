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
struct UnsocketRecipe {
    bool enabled = false;
    std::string itemType;
    std::array<std::string, 2> materials;
    int version = 0, minimumDifficulty = 0;
};
struct SocketRecipeMaterial {
    std::string code;
    unsigned quantity = 1;
    int uniqueRow = -1;
    bool type = false;
};
struct SocketRecipe {
    int row = -1, version = 0, minimumDifficulty = 0;
    std::string itemType;
    std::optional<ItemQuality> quality;
    bool requiresSockets = false, requiresNoSockets = false, rerollMagic = false;
    int minimum = 1, maximum = 1, level = 0, playerLevelPercent = 0, itemLevelPercent = 0;
    std::vector<SocketRecipeMaterial> materials;
};
} // namespace d2x
