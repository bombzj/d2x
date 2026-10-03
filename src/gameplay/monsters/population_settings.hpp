#pragma once
#include <cstdint>

namespace d2x {
struct PopulationSettings {
    uint32_t seed = 0; // Supplied by the application/session; zero is a valid explicit seed.
    int difficulty = 0; // Normal, Nightmare, Hell. Independent of loot randomness.
};
} // namespace d2x
