#include "unit_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
namespace d2x {
int32_t unitDefense(int32_t dexterity,int32_t armor,int32_t itemPercent,int32_t skillPercent,int32_t holyShieldPercent,int32_t overridePercent) {
    // UNITS_GetDefense deliberately uses ordinary native multiply/divide,
    // then a widened multiply for armor_override_percent.
    const auto base=nativeAdd(dexterity/4,armor);
    const auto percent=nativeAdd(nativeAdd(itemPercent,skillPercent),holyShieldPercent);
    auto value=nativeAdd(base,nativeMultiplyDivide(base,percent,base<=0?-100:100));
    if(overridePercent) value=nativeAdd(value,nativeNarrow(int64_t(value)*overridePercent/100));
    return value;
}
int32_t unitAttackRating(int32_t dexterity,int32_t toHit,int32_t classFactor) {
    return nativeAdd(nativeAdd(toHit,nativeMultiply(5,nativeSubtract(dexterity,7))),classFactor);
}
int32_t unitBlockChance(int32_t block,int32_t dexterity,int32_t level) {
    // UNITS_GetBlockRate normalizes level<=1 but only caps the upper end.
    return std::min(nativeMultiplyDivide(block,nativeSubtract(dexterity,15),nativeMultiply(2,std::max(level,1))),75);
}
}
