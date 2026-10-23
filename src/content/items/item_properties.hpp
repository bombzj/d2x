#pragma once
#include "gameplay/stats/stat_list.hpp"
#include "gameplay/loot/special.hpp"
#include "content/classic_data.hpp"
#include <span>
#include <optional>
#include <string_view>

namespace d2x {
std::shared_ptr<const StatRuleCatalog> prepareNativeStatRules(const ClassicData &);
struct ItemInstance;
void loadPropertyData(ClassicData &data);
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code);
SpecialPropertyRoll rollItemProperties(const ClassicData &,std::span<const PropertyRange>,uint64_t seed,int itemLevel);
std::vector<ResolvedItemStat> resolvePropertyStats(const ClassicData &data,
    const PropertyRange &property, int roll, int characterLevel, int itemLevel = -1,
    std::span<const int32_t> operations = {});
std::vector<ResolvedItemStat> resolveItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel);
unsigned itemMaximumDurability(const ClassicData &, const ItemInstance &, std::span<const ResolvedItemStat>);
std::vector<ResolvedItemStat> resolveOwnItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel);
std::vector<std::string> describeItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel, std::string_view characterClass = {});
std::vector<std::string> describeItemStats(const ClassicData &data,
    std::span<const ResolvedItemStat> stats, std::string_view characterClass = {}, int itemFileIndex = -1,
    std::optional<int> ownerMissingHp = {});
} // namespace d2x
