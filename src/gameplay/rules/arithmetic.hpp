#pragma once
#include <cstdint>

namespace d2x {
// Explicit x86 integer semantics without C++ signed-overflow undefined behavior.
int32_t nativeNarrow(int64_t value) noexcept;
int32_t nativeChecked(int64_t value);
// CVTTSD2SI: truncate toward zero; invalid signed conversion yields INT_MIN.
int32_t nativeTruncate(double value) noexcept;
int32_t nativeAdd(int32_t first, int32_t second) noexcept;
int32_t nativeSubtract(int32_t first, int32_t second) noexcept;
int32_t nativeMultiply(int32_t first, int32_t second) noexcept;
int32_t nativeShiftLeft(int32_t value, unsigned bits);
int32_t nativeDivide(int32_t value, int32_t denominator);
int32_t nativeMultiplyDivide(int32_t value, int32_t numerator, int32_t denominator);
// DATATBLS_ApplyRatio and MONSTERUNIQUE_CalculatePercentage share this algorithm.
int32_t nativePercentage(int32_t value, int32_t numerator, int32_t denominator);
// D2StatList::ComputeStatPercentage uses division in floating point, not
// DATATBLS_ApplyRatio or signed integer multiply/divide.
int32_t nativeStatPercentage(int32_t value,int32_t percent);
}
