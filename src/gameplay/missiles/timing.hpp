#pragma once
#include <cstdint>
#include <optional>
#include "gameplay/rules/arithmetic.hpp"

namespace d2x {
// D2Common MISSILE_SetTotalFrames / SetCurrentFrame store signed words.
constexpr int32_t nativeMissileFrameCount(int32_t value) noexcept {
    return value < -32768 ? -32768 : value >= 32767 ? 32767 : value;
}
struct NativeMissileInitialFrames {int32_t total{},current{},activate{},sequence{};};
inline NativeMissileInitialFrames nativeMissileInitialFrames(int32_t range,int32_t activation,std::optional<int> start={}) {
    // Constructor flag 0x200 shortens both counters and supplies sequence age.
    // A later SetCurrentFrame (including flag 0x400 / packet 73) changes one.
    if(start) range=nativeSubtract(range,*start);
    return {nativeMissileFrameCount(range),nativeMissileFrameCount(range),
        nativeMissileFrameCount(nativeSubtract(range,activation)),start?nativeShiftLeft(*start,8):0};
}
}
