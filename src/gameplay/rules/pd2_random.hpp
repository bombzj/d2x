#pragma once
#include <cstdint>

namespace d2x {
// The current core imports the Microsoft CRT's rand(), independently of
// D2's unit seeds. Keep its 15-bit sequence portable and owned by the host.
inline uint32_t pd2CrtRandom(uint32_t &state) {
    state=state*214013u+2531011u;
    return (state>>16)&0x7fffu;
}
// Current core RVA 0x36DB70 uses a distinct high-word update for its
// unit-event rolls. It does not replace D2Common's ordinary seed function.
inline uint32_t pd2UnitEventRandom(uint64_t &state) {
    const auto previousHigh=uint32_t(state>>32);
    const auto product=uint64_t(uint32_t(state))*0x6ac690c5ULL;
    const auto low=uint32_t(product);
    const auto carry=previousHigh && low>uint32_t(0x7fffffffu-previousHigh)?1u:0u;
    const auto nextHigh=uint32_t(product>>20)+carry;
    const auto nextLow=low+previousHigh;
    state=(uint64_t(nextHigh)<<32)|nextLow;
    return nextLow;
}
}
