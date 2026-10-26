#pragma once
#include "content/classic_data.hpp"
#include "gameplay/loot/quality.hpp"

namespace d2x {
ItemQualityRules loadItemQualityRules(const ClassicData &data, const DataTable &ratios,
                                     std::string_view code);
LootPlan planSelectedItem(const ClassicData &,std::string_view code,int level,uint64_t seed,
    const std::set<size_t> &used = {},std::string_view characterClass = {},
    std::optional<DropQuality> forcedQuality = {},bool superiorFloor = false,
    const ItemSeedSource &seeds = {},int preferredPrefix = 0,int preferredSuffix = 0);
LootPlan planCreatedItem(const ClassicData &,std::string_view code,int level,
    const std::set<size_t> &used,const ItemSeeds &);
LootPlan planItemLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                      int itemLevel, int upgradeLevel, uint64_t seed,
                      const std::set<size_t> &usedUniques = {}, std::string_view characterClass = {},
                      int magicFind = 0, int goldFind = 0,
                      std::optional<DropQuality> forcedQuality = std::nullopt, unsigned effectivePlayers = 1, bool ignoreNoDrop = false,
                      bool hellBovine = false, const ItemSeedSource &seeds = {}, size_t maximumItems = 6);
} // namespace d2x
