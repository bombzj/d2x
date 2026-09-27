#pragma once
#include "core/random.hpp"
#include <cstdint>

namespace d2x {
// Objects.InteractType stores the lock in bit 7 and the trap in bits 0-6.
// Keep these independent: opening a lock must not erase the trap identity.
struct ChestState {
    bool locked = false;
    bool lockable = false;
    bool sparkly = false;
    uint8_t trap = 0;
    uint64_t lootSeed = 0;
};
// Objects InitFunction03/57, shared by initial creation and a new game after loading.
inline void resetChestRandom(ChestState &chest, int objectLevel, uint64_t &random) {
    chest.trap = limitedRandom(random, 100) < unsigned(objectLevel / 8 + 5)
        ? uint8_t(limitedRandom(random, 8) + 1) : 0;
    chest.locked = chest.lockable && limitedRandom(random, 100) < unsigned(objectLevel / 2 + 8);
    chest.lootSeed = initialRandom(limitedRandom(random, 65534) + 1);
}
} // namespace d2x
