#pragma once
#include "content/classic_data.hpp"

namespace d2x {
struct ItemInstance;
class ClassicStrings;
void loadSocketData(ClassicData &content, const ClassicStrings &strings);
const RunewordRecord *matchRuneword(const ClassicData &content, const ItemInstance &item);
void prepareSocketedItem(const ClassicData &content, ItemInstance &item, uint64_t &random);
int socketRequiredLevel(const ClassicData &content, const ItemInstance &item);
std::vector<ResolvedItemStat> resolveSocketStats(const ClassicData &content,
    const ItemInstance &item, int level);
} // namespace d2x
