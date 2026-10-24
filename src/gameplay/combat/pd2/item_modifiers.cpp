#include "item_modifiers.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
void mergePd2ItemModifiers(Pd2ItemModifiers &target,const Pd2ItemModifiers &source) {
    target.criticalChance=nativeAdd(target.criticalChance,source.criticalChance);
    target.maximumDeadlyStrike=nativeAdd(target.maximumDeadlyStrike,source.maximumDeadlyStrike);
    target.criticalMultiplier=nativeAdd(target.criticalMultiplier,source.criticalMultiplier);
    target.deadlyMultiplier=nativeAdd(target.deadlyMultiplier,source.deadlyMultiplier);
    target.physicalPierce=nativeAdd(target.physicalPierce,source.physicalPierce);
    target.lifeDrainCap=nativeAdd(target.lifeDrainCap,source.lifeDrainCap);
}
Pd2CriticalRoll rollPd2Critical(const Pd2ItemModifiers &values,int passive,int mastery,int deadly,uint64_t &random) {
    const auto critical=std::min(75,nativeAdd(nativeAdd(passive,mastery),values.criticalChance));
    if(critical>0 && limitedRandom(random,100)<uint32_t(critical)) return {1,nativeAdd(200,values.criticalMultiplier)};
    // The native branch rolls whenever the original DS is positive, even
    // if its adjusted cap makes the effective chance nonpositive.
    if(deadly>0 && int(limitedRandom(random,100))<std::min(deadly,nativeAdd(75,values.maximumDeadlyStrike)))
        return {2,nativeAdd(150,values.deadlyMultiplier)};
    return {};
}
}
