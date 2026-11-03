#pragma once
#include "quality.hpp"
#include <algorithm>
#include <array>
#include <optional>
namespace d2x {
// Native 1.13c item quality IDs, shared by wire and standalone D2S adapters.
// The internal enum order is deliberately independent of the native encoding.
extern const std::array<ItemQuality, 10> nativeItemQualities;
std::optional<ItemQuality> itemQualityFromNative(unsigned quality);
// Inferior bases trade at 75% of the rolled value; the native floors differ
// per stat (defense/melee-low/thrown-high use 1, melee-high/thrown-low use 2).
template <class T> constexpr T inferiorBaseValue(T value, T floor) {
    return std::max(floor, value * T(75) / T(100));
}
} // namespace d2x
