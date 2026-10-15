#include "arithmetic.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace d2x {
int32_t nativeNarrow(int64_t value) noexcept {
    return std::bit_cast<int32_t>(uint32_t(value));
}
int32_t nativeChecked(int64_t value) {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::overflow_error("Value exceeds native signed integer range");
    return int32_t(value);
}
int32_t nativeAdd(int32_t first, int32_t second) noexcept {
    return nativeNarrow(int64_t(first) + second);
}
int32_t nativeSubtract(int32_t first, int32_t second) noexcept {
    return nativeNarrow(int64_t(first) - second);
}
int32_t nativeMultiply(int32_t first, int32_t second) noexcept {
    return nativeNarrow(int64_t(first) * second);
}
int32_t nativeShiftLeft(int32_t value, unsigned bits) {
    if (bits >= 32) throw std::invalid_argument("Invalid native integer shift");
    return std::bit_cast<int32_t>(uint32_t(value) << bits);
}
int32_t nativeDivide(int32_t value, int32_t denominator) {
    if (!denominator) return 0;
    if (value == std::numeric_limits<int32_t>::min() && denominator == -1)
        throw std::overflow_error("Native integer division overflow");
    return value / denominator;
}
int32_t nativeMultiplyDivide(int32_t value, int32_t numerator, int32_t denominator) {
    return nativeDivide(nativeMultiply(value, numerator), denominator);
}
int32_t nativePercentage(int32_t value, int32_t numerator, int32_t denominator) {
    if (!denominator) return 0;
    if (value <= 1048576) {
        if (numerator <= 65536)
            return nativeMultiplyDivide(value, numerator, denominator);
        if (denominator <= (numerator >> 4))
            return nativeMultiply(value, nativeDivide(numerator, denominator));
    } else if (denominator <= (value >> 4)) {
        return nativeMultiply(numerator, nativeDivide(value, denominator));
    }
    return nativeNarrow(int64_t(numerator) * value / denominator);
}
int32_t nativeStatPercentage(int32_t value,int32_t percent) {
    const auto result=std::trunc(double(value)*(double(percent)/100.0));
    if(!std::isfinite(result) || result<std::numeric_limits<int32_t>::min() || result>std::numeric_limits<int32_t>::max())
        throw std::overflow_error("Stat percentage exceeds native signed range");
    return int32_t(result);
}
}
