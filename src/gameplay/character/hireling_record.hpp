#pragma once
#include "core/fixed.hpp"
#include <cstdint>
#include <string>

namespace d2x {
struct HirelingRecord {
    int sourceRow = -1, classId = -1;
    std::string nameKey;
    int level = 0;
    // Native D2S only stores death status. Decode supplies the same base-life
    // marker as before; the session then derives equipped maximum life.
    Fixed8 hp;
    uint64_t experience = 0;
    uint32_t seed = 0;
};
} // namespace d2x
