#pragma once
#include "classic_data.hpp"
#include "gameplay/loot/quality.hpp"

namespace d2x {
ItemQualityRules loadItemQualityRules(const ClassicData &data, const DataTable &ratios,
                                     std::string_view code);
LootPlan planConsumableLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                           int itemLevel, int upgradeLevel, uint64_t seed);
} // namespace d2x