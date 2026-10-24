#pragma once
#include <span>
#include <cstdint>
namespace d2x {
struct ClassicData;struct CubeRecipe;struct ItemInstance;
bool matchesPd2CubeCondition(const ClassicData &,const CubeRecipe &,const ItemInstance &,int level);
bool pd2CubeCorruptionAvailable(const ClassicData &,std::span<const ItemInstance *const>,int level);
bool applyPd2DestroyedCorruption(const ClassicData &,ItemInstance &,uint64_t &itemRandom,uint32_t &programRandom);
}
