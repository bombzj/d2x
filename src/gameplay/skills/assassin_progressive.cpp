#include "assassin_progressive.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/units/direction.hpp"
#include "core/random.hpp"
#include <algorithm>

namespace d2x {
std::vector<Vec> assassinProgressiveSpiral(Vec origin,Vec target,int frames,uint64_t &random) {
    constexpr int xOffsets[]{2,2,0,-2,-2,-2,0,2},yOffsets[]{0,2,2,2,0,-2,-2,-2};
    auto x=int32_t(origin.x),y=int32_t(origin.y);
    int direction=nativeDirectionVector(int64_t(int32_t(target.x))-x,int64_t(int32_t(target.y))-y).index;
    int sign=2*int(rollRandom(random)&1)-1,step=2+int(rollRandom(random)%3);
    std::vector<Vec> points;points.reserve(size_t(std::max(frames,0)));
    for(int frame=0;frame<frames;++frame) {
        direction+=sign*step;const auto index=nativeOverlayDirection(uint8_t(direction&63));
        x=nativeAdd(x,xOffsets[index]);y=nativeAdd(y,yOffsets[index]);points.push_back({float(x)+.5f,float(y)+.5f});
        if(frame%15==0) {sign=-sign;step=2+int(rollRandom(random)%3);}
    }
    return points;
}
int32_t assassinNextCharge(int32_t previous) {return std::min(nativeAdd(previous,1),3);}
std::pair<int32_t,int32_t> assassinCobraLeech(int32_t charges,int32_t leech) {
    if(charges==1) return {leech,0};
    if(charges==2) return {leech,leech};
    if(charges==3) {const auto doubled=nativeMultiply(leech,2);return {doubled,doubled};}
    return {};
}
std::vector<AssassinProgressiveStep> assassinProgressiveSteps(int32_t charges,bool stack,
    const std::array<uint16_t,3> &functions) {
    const auto count=std::clamp(charges,1,3);std::vector<AssassinProgressiveStep> result;
    if(stack) for(int32_t index=0;index<count-1;++index)
        if(functions[size_t(index)]) result.push_back({index+1,functions[size_t(index)]});
    result.push_back({count,functions[size_t(count-1)]});
    return result;
}
std::pair<int32_t,int32_t> assassinKickRange(const AssassinKickInput &input) {
    const auto base=std::max(nativeSubtract(nativeAdd(input.strength,input.dexterity),20),1);
    auto minimum=nativeShiftLeft(base/4,8),maximum=nativeShiftLeft(base/3,8);
    minimum=nativeAdd(minimum,nativePercentage(minimum,input.skillPercent,100));
    maximum=nativeAdd(maximum,nativePercentage(maximum,input.skillPercent,100));
    auto kickMinimum=input.itemKickDamage,kickMaximum=input.itemKickDamage,percent=input.skillPercent;
    if(input.boots) {
        auto damagePercent=nativeAdd(nativeMultiplyDivide(input.bootStrengthBonus,input.strength,100),
            nativeMultiplyDivide(input.bootDexterityBonus,input.dexterity,100));
        damagePercent=std::max(nativeAdd(damagePercent,input.offWeaponDamagePercent),-90);
        // The native helper first adds ITEM_KICKDAMAGE, then adds the boot
        // range which itself includes ITEM_KICKDAMAGE. Preserve both adds.
        const auto bootMinimum=nativeAdd(input.itemKickDamage,input.bootMinimum);
        const auto bootMaximum=std::max(bootMinimum,nativeAdd(input.itemKickDamage,input.bootMaximum));
        kickMinimum=nativeAdd(kickMinimum,bootMinimum);kickMaximum=nativeAdd(kickMaximum,bootMaximum);
        percent=nativeAdd(percent,nativeAdd(input.offWeaponMaximumPercent,damagePercent));
    }
    const auto bootLow=nativeShiftLeft(kickMinimum,8),bootHigh=nativeShiftLeft(kickMaximum,8);
    minimum=nativeAdd(minimum,nativeAdd(bootLow,nativePercentage(bootLow,percent,100)));
    maximum=nativeAdd(maximum,nativeAdd(bootHigh,nativePercentage(bootHigh,percent,100)));
    return {minimum,maximum};
}
}
