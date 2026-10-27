#include "arithmetic.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
int32_t pd2MultiplyDivide(int32_t value,double multiplier,uint32_t denominator) noexcept {
    // Preserve MULSD followed by DIVSD; dividing the multiplier first changes
    // rounding. COMISD/JA also lets unordered values reach CVTTSD2SI.
    const double result=(double(value)*multiplier)/double(std::max(denominator,uint32_t{1}));
    constexpr auto maximum=std::numeric_limits<int32_t>::max();
    if(result>double(maximum)) return maximum;
    return nativeTruncate(result);
}
}
