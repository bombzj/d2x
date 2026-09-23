#pragma once
#include "affix.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace d2x {
// Row is the original TXT row (the game's file index), not a generated item ID.
struct SpecialItemRecord {
    struct Bonus {
        std::string condition;
        PropertyRange property;
    };
    size_t row = 0;
    std::string name, code, set, icon, groundAnimation;
    int level = 0, rarity = 1;
    bool noLimit = false, ladder = false, cowOnly = false;
    bool artAvailable = false;
    int requiredLevel = 0;
    std::vector<PropertyRange> properties;
    std::vector<Bonus> setBonuses;
};
struct SpecialItemRoll {
    std::optional<size_t> row;
    uint64_t randomState = 0;
    bool alreadyDropped = false;
};
struct SpecialPropertyRoll {
    std::vector<int32_t> values;
    uint64_t randomState = 0;
};
SpecialPropertyRoll rollDisplayProperties(std::span<const PropertyRange> properties, uint64_t seed);
SpecialPropertyRoll rollSpecialProperties(const SpecialItemRecord &record, uint64_t seed);
SpecialItemRoll rollSpecialItem(std::span<const SpecialItemRecord> records, std::string_view code,
                                int itemLevel, uint64_t seed, const std::set<size_t> &used = {});
} // namespace d2x
