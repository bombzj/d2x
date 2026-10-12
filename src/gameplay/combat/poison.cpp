#include "poison.hpp"
#include <algorithm>

namespace d2x {
int64_t poisonMasteryRate(int64_t rate,int percent) {
    rate=std::clamp<int64_t>(rate,0,INT32_MAX);
    return std::clamp<int64_t>(rate+rate*percent/100,0,INT32_MAX);
}
int weaponPoisonLength(int64_t itemFrames,int itemSources,int64_t skillFrames) {
    // SUnitDmg: Poison Dagger supplies length before equipment length/count;
    // it is not another STAT_POISON_COUNT source.
    return int(std::clamp<int64_t>((itemFrames+skillFrames)/std::max(1,itemSources),0,INT32_MAX));
}
}
