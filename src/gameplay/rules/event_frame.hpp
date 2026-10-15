#pragma once
#include "arithmetic.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
namespace d2x {
struct NativeEventDeadline {uint64_t due{};bool continuous{};};
// EVENT_SetEvent: -1 is a continuous queue; other signed deadlines at or
// before the current native frame are inserted for the following frame.
inline NativeEventDeadline nativeEventDeadline(uint64_t tick,int32_t frame) {
    if(tick==UINT64_MAX) throw std::overflow_error("Simulation event frame exhausted");
    const auto current=nativeNarrow(uint32_t(tick));
    if(frame==-1) return {tick+1,true};
    if(frame<=current) return {tick+1,false};
    const auto distance=uint32_t(uint32_t(frame)-uint32_t(current));
    if(distance>UINT64_MAX-tick) throw std::overflow_error("Simulation event frame exhausted");
    return {tick+distance,false};
}
}
