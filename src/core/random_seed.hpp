#pragma once
#include <cstdint>
#include <random>

namespace d2x {
// Platform-independent boundary entropy. Gameplay uses D2Seed, not this engine.
// Failure propagates instead of silently falling back to a fixed seed.
inline uint32_t freshSeed() {
    std::random_device entropy;
    return std::uniform_int_distribution<uint32_t>{}(entropy);
}
} // namespace d2x
