#pragma once
#include <cstdint>
namespace d2x {
// D2Seed recurrence, used by native DRLG rules. See docs/licenses/D2MOO.txt.
class Seed {
    uint64_t state_;

  public:
    explicit Seed(uint32_t low) : state_((uint64_t(666) << 32) | low) {}
    uint32_t next() {
        state_ = uint64_t(uint32_t(state_)) * 0x6ac690c5ULL + (state_ >> 32);
        return uint32_t(state_);
    }
    int below(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
};
} // namespace d2x
