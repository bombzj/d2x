#pragma once
#include "gameplay/items/quality.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace d2x {
struct ItemPropertyTarget {
    std::string code, parameter;
    std::optional<int> value, minimum, maximum;
};
struct ItemSeeds { uint32_t unit{}, properties{}; };
using ItemSeedSource = std::function<ItemSeeds()>;
struct ItemAffixInstance {
    bool prefix = false;
    int32_t row = -1;
    std::vector<int32_t> propertyRolls;
    std::vector<std::vector<int32_t>> operationRolls;
};
struct ItemGeneration {
    ItemQuality quality = ItemQuality::Normal;
    int32_t specialRow = -1;
    int32_t requiredLevel = 0;
    int32_t gradeRow = -1;
    int32_t rarePrefixRow = -1, rareSuffixRow = -1;
    std::vector<int32_t> propertyRolls;
    std::vector<ItemAffixInstance> affixes;
    bool rollNaturalSockets = false;
    bool rollNaturalEthereal = false;
    unsigned durabilityMultiplier = 1;
    int socketDifficulty = 0;
    std::optional<ItemSeeds> seeds;
    std::optional<uint64_t> propertyRandom;
    std::optional<uint64_t> unitRandom;
    std::vector<std::vector<int32_t>> operationRolls;
};
} // namespace d2x
