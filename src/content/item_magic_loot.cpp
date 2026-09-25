#include "item_magic_loot.hpp"
#include "gameplay/loot/affix.hpp"
#include "gameplay/loot/special.hpp"
#include <algorithm>

namespace d2x {
AffixGenerationResult rollAffixItem(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, int itemLevel, uint64_t seed,
                                    std::string_view /*characterClass*/) {
    AffixGenerationResult result;
    result.randomState = seed;
    result.generation.quality = quality;
    if (item.equipment.types.empty() ||
        (quality != ItemQuality::Magic && quality != ItemQuality::Rare)) {
        result.deferred = "Unsupported affix item base: " + item.code;
        return result;
    }
    const int magicLevel = item.base.magicLevel.value_or(0);
    const int affixLevel = itemAffixLevel(itemLevel, item.base.level.value_or(0), magicLevel);
    const bool socketable = item.base.sockets.value_or(0) > 0;
    std::vector<int> usedGroups[2];
    if (quality == ItemQuality::Rare) {
        auto chooseName = [&](std::span<const RareNameRecord> records) -> int32_t {
            std::vector<const RareNameRecord *> candidates;
            for (const auto &record : records) {
                auto matches = [&](const std::string &type) { return item.equipment.isType(type); };
                if (std::any_of(record.excludedTypes.begin(), record.excludedTypes.end(), matches) ||
                    !std::any_of(record.includedTypes.begin(), record.includedTypes.end(), matches))
                    continue;
                candidates.push_back(&record);
            }
            if (candidates.empty())
                return -1;
            result.randomState = uint64_t(uint32_t(result.randomState)) * 0x6ac690c5ULL +
                                 (result.randomState >> 32);
            return int32_t(candidates[uint32_t(result.randomState) % candidates.size()]->row);
        };
        result.generation.rarePrefixRow = chooseName(std::span<const RareNameRecord>(data.rarePrefixes));
        result.generation.rareSuffixRow = chooseName(std::span<const RareNameRecord>(data.rareSuffixes));
        if (result.generation.rarePrefixRow < 0 || result.generation.rareSuffixRow < 0) {
            result.deferred = "No eligible original rare name: " + item.code;
            return result;
        }
    }
    auto append = [&](bool prefix, bool force) {
        const auto &records = prefix ? data.magicPrefixes : data.magicSuffixes;
        // D2MOO checks the item's class restriction, not the buyer/killer's class.
        auto roll = rollMagicAffix(records, std::span<const std::string>(item.equipment.types), item.equipment.requiredClass,
                                   affixLevel, magicLevel, quality == ItemQuality::Rare, socketable,
                                   std::span<const int>(usedGroups[prefix]), force,
                                   result.randomState);
        result.randomState = roll.randomState;
        if (!roll.row)
            return false;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return record.row == *roll.row; });
        if (found == records.end())
            return false;
        auto properties = rollDisplayProperties(std::span<const PropertyRange>(found->properties),
                                                result.randomState);
        result.randomState = properties.randomState;
        result.generation.affixes.push_back({prefix, int32_t(found->row), std::move(properties.values)});
        result.generation.requiredLevel = std::max(result.generation.requiredLevel,
                                                    found->requiredLevel);
        usedGroups[prefix].push_back(found->group);
        return true;
    };
    if (quality == ItemQuality::Magic) {
        bool prefix = append(true, false);
        append(false, !prefix);
    } else {
        auto below = [&](uint32_t limit) {
            result.randomState = uint64_t(uint32_t(result.randomState)) * 0x6ac690c5ULL +
                                 (result.randomState >> 32);
            return uint32_t(result.randomState) % limit;
        };
        // ItemsMagic::sub_6FC53760: rare jewels request 3-4 affixes,
        // other rares 4-6. The item-level rule belongs to crafted items.
        int target = item.equipment.isType("jewl") ? 3 + int(below(2)) : 4 + int(below(3));
        int prefixes = 0, suffixes = 0;
        for (int attempt = 0; attempt < target && (prefixes < 3 || suffixes < 3); ++attempt) {
            bool choosePrefix = suffixes >= 3 || (prefixes < 3 && below(2) == 0);
            if (append(choosePrefix, true))
                (choosePrefix ? prefixes : suffixes)++;
        }
    }
    if (result.generation.affixes.empty())
        result.deferred = "No eligible original affix: " + item.code;
    return result;
}
} // namespace d2x
