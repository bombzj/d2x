#pragma once
#include "gameplay/items/modifiers.hpp"
#include <cstdint>
#include <vector>
namespace d2x {
struct ClassicData;struct PropertyRange;
int rollPd2SorceressProperty(uint64_t &random);
std::vector<ResolvedItemStat> resolvePd2SorceressProperty(const ClassicData &,const PropertyRange &,int skill,int level);
}
