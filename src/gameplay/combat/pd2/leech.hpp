#pragma once
#include <cstdint>
namespace d2x {
// Current core 0x2FC030 gates the write; it does not clamp a crossing gain.
inline bool pd2LifeDrainWriteAllowed(int32_t previous,int32_t next,int32_t maximum,int cap) {
    const auto limit=(100.0-double(cap))/100.0;
    const auto denominator=double(uint32_t(maximum?maximum:1));
    return double(next)/denominator<limit || double(uint32_t(previous))/denominator<limit;
}
}
