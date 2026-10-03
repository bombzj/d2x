#pragma once
#include "core/id.hpp"
#include "core/math.hpp"
#include <cstdint>

namespace d2x {
struct SkillProjectileSource {
    EntityId id;
    Vec pos, look;
    uint64_t &combatRandom;
};
} // namespace d2x
