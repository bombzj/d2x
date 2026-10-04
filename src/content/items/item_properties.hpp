#pragma once
#include "content/classic_data.hpp"

namespace d2x {
struct ItemInstance;
void loadPropertyData(ClassicData &data);
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code);
std::vector<ResolvedItemStat> resolvePropertyStats(const ClassicData &data,
    const PropertyRange &property, int roll, int characterLevel, int itemLevel = -1);
std::vector<ResolvedItemStat> resolveItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel);
std::vector<ResolvedItemStat> resolveOwnItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel);
std::vector<std::string> describeItemStats(const ClassicData &data,
    const ItemInstance &item, int characterLevel);
} // namespace d2x
