#pragma once
#include <algorithm>
#include <array>
#include <optional>
namespace d2x {
enum class ItemQuality { Normal, Magic, Rare, Set, Unique, Superior, Inferior, Crafted, Tempered };
// Native 1.13c item quality IDs, shared by wire and standalone D2S adapters.
// The internal enum order is deliberately independent of the native encoding.
inline constexpr std::array<ItemQuality, 10> nativeItemQualities{ItemQuality::Normal, ItemQuality::Inferior,
    ItemQuality::Normal, ItemQuality::Superior, ItemQuality::Magic, ItemQuality::Set,
    ItemQuality::Rare, ItemQuality::Unique, ItemQuality::Crafted, ItemQuality::Tempered};
inline std::optional<ItemQuality> itemQualityFromNative(unsigned quality) {
    if (!quality || quality >= nativeItemQualities.size()) return std::nullopt;
    return nativeItemQualities[quality];
}
// Inferior bases trade at 75% of the rolled value; the native floors differ
// per stat (defense/melee-low/thrown-high use 1, melee-high/thrown-low use 2).
template <class T> constexpr T inferiorBaseValue(T value, T floor) {
    return std::max(floor, value * T(75) / T(100));
}
} // namespace d2x
