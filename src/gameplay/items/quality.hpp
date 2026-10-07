#pragma once
#include <array>
#include <optional>
namespace d2x {
enum class ItemQuality { Normal, Magic, Rare, Set, Unique, Superior, Inferior, Crafted };
// Native 1.13c item quality IDs, shared by wire and standalone D2S adapters.
// The internal enum order is deliberately independent of the native encoding.
inline constexpr std::array<ItemQuality, 9> nativeItemQualities{ItemQuality::Normal, ItemQuality::Inferior,
    ItemQuality::Normal, ItemQuality::Superior, ItemQuality::Magic, ItemQuality::Set,
    ItemQuality::Rare, ItemQuality::Unique, ItemQuality::Crafted};
inline std::optional<ItemQuality> itemQualityFromNative(unsigned quality) {
    if (!quality || quality >= nativeItemQualities.size()) return std::nullopt;
    return nativeItemQualities[quality];
}
} // namespace d2x
