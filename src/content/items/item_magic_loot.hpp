#pragma once
#include "content/classic_data.hpp"

namespace d2x {
struct AffixGenerationResult {
    ItemGeneration generation;
    uint64_t randomState = 0;
    std::string deferred;
};
AffixGenerationResult rollAffixItem(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, int itemLevel, uint64_t seed,
                                    std::string_view characterClass);
} // namespace d2x
