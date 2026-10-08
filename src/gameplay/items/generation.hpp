#pragma once
#include "gameplay/items/quality.hpp"
#include <cstdint>
#include <vector>

namespace d2x {
struct ItemAffixInstance {
    bool prefix = false;
    int32_t row = -1;
    std::vector<int32_t> propertyRolls;
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
};
} // namespace d2x
