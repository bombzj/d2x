#include "special.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace d2x {
SpecialItemRoll rollSpecialItem(std::span<const SpecialItemRecord> records, std::string_view code,
                                int itemLevel, uint64_t seed, const std::set<size_t> &used) {
    if (itemLevel < 1 || itemLevel > 99)
        throw std::invalid_argument("Invalid special item level");
    int64_t total = 0;
    std::vector<const SpecialItemRecord *> eligible;
    for (const auto &record : records) {
        if (record.code != code || record.level > itemLevel || record.ladder || record.cowOnly)
            continue;
        if (record.rarity < 0 || total > std::numeric_limits<int>::max() - std::max(1, record.rarity))
            throw std::runtime_error("Invalid special item rarity");
        eligible.push_back(&record);
        total += std::max(1, record.rarity);
    }
    SpecialItemRoll result;
    result.randomState = seed;
    if (!total)
        return result;
    result.randomState = uint64_t(uint32_t(seed)) * 0x6ac690c5ULL + (seed >> 32);
    int64_t chosen = uint32_t(result.randomState) % uint32_t(total);
    for (const auto *record : eligible) {
        chosen -= std::max(1, record->rarity);
        if (chosen < 0) {
            result.row = record->row;
            result.alreadyDropped = !record->noLimit && used.contains(record->row);
            return result;
        }
    }
    throw std::logic_error("Special item weight selection failed");
}
SpecialPropertyRoll rollDisplayProperties(std::span<const PropertyRange> properties, uint64_t seed) {
    SpecialPropertyRoll result;
    result.randomState = seed;
    for (const auto &property : properties) {
        int32_t value = property.minimum.value_or(0);
        if (property.directRoll && property.minimum && property.maximum &&
            *property.maximum > *property.minimum) {
            uint64_t width = uint64_t(int64_t(*property.maximum) - *property.minimum) + 1;
            if (width > uint64_t(std::numeric_limits<uint32_t>::max()))
                throw std::runtime_error("Special property range exceeds supported random width");
            result.randomState = uint64_t(uint32_t(result.randomState)) * 0x6ac690c5ULL +
                                 (result.randomState >> 32);
            value = int32_t(int64_t(*property.minimum) + uint32_t(result.randomState) % uint32_t(width));
        }
        result.values.push_back(value);
    }
    return result;
}
SpecialPropertyRoll rollSpecialProperties(const SpecialItemRecord &record, uint64_t seed) {
    return rollDisplayProperties(record.properties, seed);
}
} // namespace d2x
