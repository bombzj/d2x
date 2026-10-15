#include "shield.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
FixedShieldAbsorption absorbManaShieldFixed(int64_t damage, int64_t mana, int percent, int manaFactor) {
    manaFactor = std::max(1, manaFactor);
    const auto fixed = std::max<int64_t>(0, damage);
    if (fixed > std::numeric_limits<int32_t>::max() || mana < 0 || mana > std::numeric_limits<int32_t>::max())
        throw std::overflow_error("Mana shield input exceeds native range");
    if (!fixed || percent <= 0) return {fixed, mana};
    const auto absorbed = std::min(nativePercentage(int32_t(fixed), percent, 100),
                                   nativePercentage(int32_t(mana), 16, manaFactor));
    return {fixed - absorbed, std::max<int64_t>(0, mana - nativePercentage(absorbed, manaFactor, 16))};
}
}