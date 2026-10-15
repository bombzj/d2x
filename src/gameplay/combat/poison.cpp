#include "poison.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
int64_t poisonMasteryRate(int64_t rate,int percent) {
    const auto fixed = nativeChecked(rate);
    return nativeAdd(fixed, nativePercentage(fixed, percent, 100));
}
int weaponPoisonLength(int64_t itemFrames,int itemSources,int64_t skillFrames,int32_t overrideFrames) {
    // SUnitDmg: Poison Dagger supplies length before equipment length/count;
    // it is not another STAT_POISON_COUNT source.
    if(overrideFrames>0) return nativeAdd(nativeNarrow(skillFrames),overrideFrames);
    return nativeDivide(nativeAdd(nativeNarrow(itemFrames),nativeNarrow(skillFrames)),std::max(1,itemSources));
}
}
