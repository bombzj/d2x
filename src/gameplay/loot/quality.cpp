#include "quality.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
const char *dropQualityName(DropQuality quality) {
    switch (quality) {
    case DropQuality::Unique: return "Unique";
    case DropQuality::Set: return "Set";
    case DropQuality::Rare: return "Rare";
    case DropQuality::Magic: return "Magic";
    case DropQuality::Superior: return "Superior";
    case DropQuality::Normal: return "Normal";
    case DropQuality::Inferior: return "Inferior";
    }
    return "Unknown";
}
QualityRoll rollItemQuality(const ItemQualityRules &rules, int itemLevel, int magicFind,
                           const std::array<int, 4> &modifiers, uint64_t seed) {
    if (itemLevel < 1 || itemLevel > 99 || rules.baseLevel < 0 || rules.baseLevel > 99 ||
        magicFind < 0 || magicFind > 1000000)
        throw std::runtime_error("Unsupported item quality level or magic find");
    for (int modifier : modifiers)
        if (modifier < 0 || modifier > 1024)
            throw std::runtime_error("Unsupported TC quality modifier");
    QualityRoll result;
    result.randomState = seed;
    if (rules.normalOnly || rules.uniqueOnly) {
        result.quality = rules.normalOnly ? DropQuality::Normal : DropQuality::Unique;
        return result;
    }
    const int difference = itemLevel - rules.baseLevel;
    for (size_t index = 0; index < rules.ratios.size(); ++index) {
        if (index == 2 && !rules.rareAllowed)
            continue;
        const auto quality = DropQuality(index);
        if (index == 3 && rules.magicOnly) {
            result.quality = quality;
            return result;
        }
        const auto &ratio = rules.ratios[index];
        if (ratio.base < 0 || ratio.divisor <= 0 || ratio.minimum < 0)
            throw std::runtime_error("Invalid original quality ratio");
        const int64_t base = int64_t(ratio.base) - difference / ratio.divisor;
        int64_t chance = base * 128;
        if (index < modifiers.size()) {
            if (magicFind != 0) {
                int64_t effective = magicFind;
                if (magicFind > 10 && index < 3) {
                    constexpr int limits[] = {250, 500, 600};
                    effective = int64_t(limits[index]) * magicFind / (magicFind + limits[index]);
                }
                chance = 12800 * base / (100 + effective);
            }
            chance = std::max<int64_t>(chance, ratio.minimum);
            chance -= chance * modifiers[index] / 1024;
        }
        if (chance > std::numeric_limits<int>::max())
            throw std::runtime_error("Quality probability exceeds supported range");
        int64_t roll = -1;
        if (chance > 0) {
            result.randomState = uint64_t(uint32_t(result.randomState)) * 0x6ac690c5ULL +
                                 (result.randomState >> 32);
            roll = uint32_t(result.randomState) % uint32_t(chance);
        }
        result.checks.push_back({quality, chance, roll});
        if (chance <= 0 || roll < 128) {
            result.quality = quality;
            return result;
        }
    }
    return result;
}
} // namespace d2x