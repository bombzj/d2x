#pragma once
#include <cstdint>

namespace d2x {
// Original STAT_ALIGNMENT values; MonStats.Align uses a different ordering.
enum class UnitAlignment : uint8_t { Evil = 0, Neutral = 1, Good = 2 };
// D2Common STATLIST_AreUnitsAligned. Identity is checked by the caller:
// distinct neutral units are not allies, even though their values are equal.
constexpr bool sameAlignment(UnitAlignment first, UnitAlignment second) {
    return first == second && (first == UnitAlignment::Evil || first == UnitAlignment::Good);
}
}
