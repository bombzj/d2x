#pragma once
#include <cstdint>

namespace d2x {
struct FixedShieldAbsorption {
    int64_t damage{}, mana{};
};
FixedShieldAbsorption absorbManaShieldFixed(int64_t damage, int64_t mana, int percent, int manaFactor);
}