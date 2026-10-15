#pragma once
#include "core/random.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x {
// SrvDo5 and retail CltDo5 use the animation sequence, not lifetime/elapsed.
inline int32_t firewallMissileSequence(int32_t precise,int32_t current,int32_t start,int32_t stop,uint64_t &random) {
    const auto frame=precise>>8;
    if(frame==nativeSubtract(start,1))
        return nativeShiftLeft(nativeAdd(frame,int32_t(limitedRandom(random,uint32_t(nativeSubtract(stop,start))))),8);
    if(current==start) return nativeShiftLeft(nativeSubtract(start,3),8);
    if(current<start) return nativeShiftLeft(std::max(nativeSubtract(frame,2),0),8);
    return precise;
}
struct MissileAnimationRule {
    int32_t rate{},frames{},subStart{},subStop{};
    bool loop{},subLoop{};
};
inline int32_t missileAnimationSpeed(int32_t rate) {
    return nativeShiftLeft(int16_t(uint16_t(rate)),8)/1024;
}
// D2Client's common Do handler calls UNITS_UpdateFrame or SetFrameNonRate
// before decrementing CurrentFrame. Neither operation uses modulo/multi-wrap.
inline int32_t advanceMissileAnimation(int32_t precise,int32_t current,const MissileAnimationRule &rule) {
    const auto next=nativeAdd(precise,missileAnimationSpeed(rule.rate));
    const auto count=nativeShiftLeft(rule.frames,8);
    if(!rule.loop && next>=count) return precise;
    if(rule.loop && rule.subLoop && current>nativeAdd(nativeSubtract(rule.frames,rule.subStop),1)) {
        const auto end=nativeShiftLeft(rule.subStop,8);
        return next>=end?nativeSubtract(next,nativeShiftLeft(nativeSubtract(rule.subStop,rule.subStart),8)):next;
    }
    return next>=count?nativeSubtract(next,count):next;
}
}
