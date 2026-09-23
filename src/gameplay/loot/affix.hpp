#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
struct PropertyRange {
    std::string code, parameter;
    std::optional<int> minimum, maximum;
    bool directRoll = false;
};
struct MagicAffixRecord {
    size_t row = 0;
    std::string name, characterClass;
    int level = 0, maxLevel = 0, requiredLevel = 0, frequency = 0, group = 0;
    bool rareAllowed = false;
    std::vector<std::string> includedTypes, excludedTypes;
    std::vector<PropertyRange> properties;
};
struct MagicAffixRoll {
    std::optional<size_t> row;
    uint64_t randomState = 0;
};
struct RareNameRecord {
    size_t row = 0;
    std::string name;
    std::vector<std::string> includedTypes, excludedTypes;
};
int itemAffixLevel(int itemLevel, int baseLevel, int magicLevel);
// The caller supplies original affix and magic levels. No MPQ access or effects.
MagicAffixRoll rollMagicAffix(std::span<const MagicAffixRecord> records,
                              std::span<const std::string> itemTypes, std::string_view characterClass,
                              int affixLevel, int magicLevel, bool rare, bool socketable,
                              std::span<const int> usedGroups, bool force, uint64_t seed);
} // namespace d2x
