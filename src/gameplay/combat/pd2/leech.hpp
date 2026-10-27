#pragma once
#include <cstdint>
#include <algorithm>
#include <limits>
namespace d2x {
// Current core 0x2FC030 gates the write; it does not clamp a crossing gain.
inline bool pd2LifeDrainWriteAllowed(int32_t previous,int32_t next,int32_t maximum,int cap) {
    const auto limit=(100.0-double(uint32_t(cap)))/100.0;
    const auto denominator=double(uint32_t(maximum?maximum:1));
    return double(next)/denominator<limit || double(uint32_t(previous))/denominator<limit;
}
// Core 306E80 adds in signed 64 bits before the max-life clamp, then
// saturates an invalid signed result. 2FC030 may refuse the complete write.
inline int32_t pd2SafeLifeAfterGain(int32_t previous,int32_t gain,int32_t maximum,int cap) {
    if(gain<=0) return previous;
    const auto sum=std::min(int64_t(previous)+gain,int64_t(maximum));
    const auto next=sum<0 || sum>std::numeric_limits<int32_t>::max()?
        std::numeric_limits<int32_t>::max():int32_t(sum);
    return pd2LifeDrainWriteAllowed(previous,next,maximum,cap)?next:previous;
}
}
