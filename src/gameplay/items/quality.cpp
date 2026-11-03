#include "quality_rules.hpp"

namespace d2x {
constexpr std::array<ItemQuality, 10> nativeItemQualities{ItemQuality::Normal, ItemQuality::Inferior,
    ItemQuality::Normal, ItemQuality::Superior, ItemQuality::Magic, ItemQuality::Set,
    ItemQuality::Rare, ItemQuality::Unique, ItemQuality::Crafted, ItemQuality::Tempered};

std::optional<ItemQuality> itemQualityFromNative(unsigned quality) {
    if (!quality || quality >= nativeItemQualities.size()) return std::nullopt;
    return nativeItemQualities[quality];
}
} // namespace d2x
