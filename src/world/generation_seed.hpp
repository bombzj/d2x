#pragma once
#include "core/random.hpp"
namespace d2x {
// D2Seed recurrence, used by native DRLG rules. See docs/licenses/D2MOO.txt.
class Seed {
    uint64_t state_;

  public:
    explicit Seed(uint32_t low) : state_(initialRandom(low)) {}
    uint32_t next() {
        return rollRandom(state_);
    }
    int below(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
};
} // namespace d2x
