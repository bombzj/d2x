#include "core/random.hpp"
#include "affix.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
int itemAffixLevel(int itemLevel, int baseLevel, int magicLevel) {
    if (itemLevel < 1 || itemLevel > 127 || baseLevel < 0 || baseLevel > 99 ||
        magicLevel < 0 || magicLevel > 99)
        throw std::invalid_argument("Invalid item affix level inputs");
    const int level = std::max(itemLevel, baseLevel);
    if (magicLevel)
        return std::clamp(magicLevel + level, 1, 99);
    const int half = baseLevel / 2;
    return std::clamp(level >= 99 - half ? 2 * level - half - (99 - half) : level - half, 1, 99);
}
MagicAffixRoll rollMagicAffix(std::span<const MagicAffixRecord> records,
                              std::span<const std::string> itemTypes, std::string_view characterClass,
                              int affixLevel, int magicLevel, bool rare, bool socketable,
                              std::span<const int> usedGroups, bool force, uint64_t seed,
                              int preferredAffix) {
    if (affixLevel < 0 || affixLevel > 99 || magicLevel < 0 || magicLevel > 99)
        throw std::invalid_argument("Invalid magic affix level");
    MagicAffixRoll result;
    result.randomState = seed;
    auto below = [&](uint32_t bound) {
        rollRandom(result.randomState);
        return uint32_t(result.randomState) % bound;
    };
    const auto coin = below(2);
    if (!force && !coin)
        return result;
    std::vector<const MagicAffixRecord *> eligible;
    int64_t total = 0;
    auto matches = [&](std::string_view type) {
        return std::find(itemTypes.begin(), itemTypes.end(), type) != itemTypes.end();
    };
    for (const auto &record : records) {
        if (record.frequency <= 0 || record.level > affixLevel ||
            (record.maxLevel && affixLevel > record.maxLevel) || (rare && !record.rareAllowed) ||
            (!characterClass.empty() && !record.characterClass.empty() && record.characterClass != characterClass) ||
            std::find(usedGroups.begin(), usedGroups.end(), record.group) != usedGroups.end() ||
            (!socketable && !record.properties.empty() && record.properties.front().code == "sock") ||
            std::any_of(record.excludedTypes.begin(), record.excludedTypes.end(), matches) ||
            !std::any_of(record.includedTypes.begin(), record.includedTypes.end(), matches))
            continue;
        if (preferredAffix > 0 && record.row == size_t(preferredAffix - 1)) {
            result.row = record.row;
            return result;
        }
        int64_t weight = int64_t(record.frequency) * (magicLevel ? record.level : 1);
        if (total >= std::numeric_limits<int>::max() - weight)
            throw std::runtime_error("Magic affix weight exceeds supported range");
        if (eligible.size() < 511)
            eligible.push_back(&record);
        total += weight;
    }
    if (eligible.empty())
        return result;
    int64_t chosen = below(uint32_t(total + 1));
    for (const auto *record : eligible) {
        chosen -= int64_t(record->frequency) * (magicLevel ? record->level : 1);
        if (chosen < 0) {
            result.row = record->row;
            return result;
        }
    }
    result.row = eligible.back()->row;
    return result;
}
} // namespace d2x
