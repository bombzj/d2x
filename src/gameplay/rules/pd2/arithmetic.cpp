#include "arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace d2x {
int32_t pd2MultiplyDivide(int32_t value,double multiplier,uint32_t denominator) noexcept {
    // Preserve MULSD followed by DIVSD; dividing the multiplier first changes
    // rounding. COMISD/JA also lets unordered values reach CVTTSD2SI.
    const double result=(double(value)*multiplier)/double(std::max(denominator,uint32_t{1}));
    constexpr auto maximum=std::numeric_limits<int32_t>::max();
    constexpr auto minimum=std::numeric_limits<int32_t>::min();
    if(result>double(maximum)) return maximum;
    const double truncated=std::trunc(result);
    if(!std::isfinite(truncated) || truncated<double(minimum)) return minimum;
    return int32_t(truncated);
}
}
