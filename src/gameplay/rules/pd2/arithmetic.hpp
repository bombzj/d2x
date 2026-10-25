#pragma once
#include <cstdint>

namespace d2x {
// Current core 17d5, RVA 37A6F0. The denominator is unsigned; only the
// positive result is capped. Invalid signed conversion returns INT_MIN.
int32_t pd2MultiplyDivide(int32_t value,double multiplier,uint32_t denominator) noexcept;
}
