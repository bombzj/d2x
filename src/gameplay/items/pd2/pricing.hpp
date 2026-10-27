#pragma once
#include "gameplay/rules/pd2/arithmetic.hpp"

namespace d2x {
// 41d49 385020 passes 100-reducedprices to 37C510 without clamping.
// Preserve both the native double operation order and its DWORD result.
inline unsigned pd2MapGamblePrice(int reducedPrices) noexcept {
    return uint32_t(pd2MultiplyDivide(1000000,100.0-double(reducedPrices),100));
}
}
