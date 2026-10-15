#pragma once
#include "content/classic_data.hpp"
#include "gameplay/items/generation.hpp"
#include <span>

namespace d2x {
struct AffixGenerationResult {
    ItemGeneration generation;
    uint64_t randomState = 0;
    std::string deferred;
};
AffixGenerationResult generatePropertyItem(const ClassicData &,const ItemDefinition &,ItemQuality,
    int itemLevel,uint64_t seed,std::span<const ItemPropertyTarget>);
AffixGenerationResult rollAffixItem(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, int itemLevel, uint64_t seed,
                                    std::string_view characterClass, int preferredPrefix = 0, int preferredSuffix = 0);
} // namespace d2x
