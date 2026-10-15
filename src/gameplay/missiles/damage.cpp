#include "damage.hpp"
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
namespace {
int32_t checkedDamage(int64_t value) {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Missile damage exceeds native fixed-point range");
    return int32_t(value);
}
}
MissileDamageRange missileDamageRange(float minimum, float maximum) {
    const auto fixed = [](float value) {
        const double scaled = double(value) * 256.0;
        if (!std::isfinite(scaled) || scaled < std::numeric_limits<int32_t>::min() ||
            scaled > std::numeric_limits<int32_t>::max())
            throw std::invalid_argument("Missile damage exceeds native fixed-point range");
        return int32_t(scaled);
    };
    return {fixed(minimum), fixed(maximum)};
}
MissileDamageRange scaleMissileDamage(MissileDamageRange range, int percent) {
    return {nativeMultiplyDivide(range.minimum,percent,100),nativeMultiplyDivide(range.maximum,percent,100)};
}
MissileDamageRange addMissileDamage(MissileDamageRange range, MissileDamageRange addition) {
    return {nativeAdd(range.minimum,addition.minimum),nativeAdd(range.maximum,addition.maximum)};
}
int32_t rollMissileDamage(MissileDamageRange range,uint64_t &random) {
    return rollMissileDamageValue(range,0,random);
}
int32_t rollMissileDamageValue(MissileDamageRange range,int32_t mastery,uint64_t &random) {
    if(range.maximum<=0 || range.minimum<=0) return 0;
    if(range.minimum>range.maximum) std::swap(range.minimum,range.maximum);
    if(mastery) {
        range.minimum=nativeAdd(range.minimum,nativePercentage(range.minimum,mastery,100));
        range.maximum=nativeAdd(range.maximum,nativePercentage(range.maximum,mastery,100));
    }
    return nativeAdd(range.minimum,int32_t(limitedRandom(random,uint32_t(nativeSubtract(range.maximum,range.minimum)))));
}
MissileDamageRange fixedMissileDamageRange(int64_t minimum, int64_t maximum) {
    return {checkedDamage(minimum), checkedDamage(maximum)};
}
}
