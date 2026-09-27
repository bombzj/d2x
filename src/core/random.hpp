#pragma once
#include <cstdint>

namespace d2x {
// D2Common D2Seed.h/.cpp: SEED_InitLowSeed and SEED_RollRandomNumber.
// 666 is the native high word, not a fixed game seed. Zero is a valid low word.
constexpr uint64_t initialRandom(uint32_t low) { return (uint64_t(666) << 32) | low; }
inline uint32_t rollRandom(uint64_t &state) {
    state = uint64_t(uint32_t(state)) * 0x6ac690c5ULL + (state >> 32);
    return uint32_t(state);
}
inline uint32_t limitedRandom(uint64_t &state, uint32_t bound) {
    return bound ? rollRandom(state) % bound : 0;
}
// SUNIT_InitSeed: advance the parent, then initialize the child with that low word.
inline uint64_t childRandom(uint64_t &parent) { return initialRandom(rollRandom(parent)); }
} // namespace d2x
