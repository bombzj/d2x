#pragma once
#include <cstdint>

namespace d2x {
// Objects.InteractType stores the lock in bit 7 and the trap in bits 0-6.
// Keep these independent: opening a lock must not erase the trap identity.
struct ChestState {
    bool locked = false;
    bool sparkly = false;
    uint8_t trap = 0;
    uint64_t lootSeed = 0;
};
} // namespace d2x
