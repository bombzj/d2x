#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace d2x {
enum class DropQuality { Unique, Set, Rare, Magic, Superior, Normal, Inferior };
struct QualityRatio {
    int base = 0, divisor = 1, minimum = 0;
};
struct ItemQualityRules {
    int baseLevel = 0;
    bool normalOnly = false, uniqueOnly = false, magicOnly = false, rareAllowed = false;
    std::array<QualityRatio, 6> ratios;
};
struct QualityCheck {
    DropQuality quality;
    int64_t chance = 0;
    int64_t roll = -1;
};
struct QualityRoll {
    DropQuality quality = DropQuality::Inferior;
    uint64_t randomState = 0;
    std::vector<QualityCheck> checks;
};
const char *dropQualityName(DropQuality quality);
QualityRoll rollItemQuality(const ItemQualityRules &rules, int itemLevel, int magicFind,
                           const std::array<int, 4> &modifiers, uint64_t seed);
} // namespace d2x