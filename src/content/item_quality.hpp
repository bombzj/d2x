#pragma once
#include "classic_data.hpp"
#include "gameplay/loot/quality.hpp"

namespace d2x {
ItemQualityRules loadItemQualityRules(const ClassicData &data, const DataTable &ratios,
                                     std::string_view code);
LootPlan planItemLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                      int itemLevel, int upgradeLevel, uint64_t seed,
                      const std::set<size_t> &usedUniques = {});
} // namespace d2x
