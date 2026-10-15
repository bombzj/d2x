#include "core/random.hpp"
#include "item_magic_loot.hpp"
#include "gameplay/loot/affix.hpp"
#include "gameplay/loot/special.hpp"
#include <algorithm>

namespace d2x {
AffixGenerationResult rollAffixItem(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, int itemLevel, uint64_t seed,
                                    std::string_view /*characterClass*/, int preferredPrefix, int preferredSuffix) {
    AffixGenerationResult result;
    result.randomState = seed;
    result.generation.quality = quality;
    if (item.equipment.types.empty() ||
        (quality != ItemQuality::Magic && quality != ItemQuality::Rare && quality != ItemQuality::Crafted)) {
        result.deferred = "Unsupported affix item base: " + item.code;
        return result;
    }
    const int magicLevel = item.base.magicLevel.value_or(0);
    const int affixLevel = itemAffixLevel(itemLevel, item.base.level.value_or(0), magicLevel);
    const bool socketable = item.base.sockets.value_or(0) > 0;
    std::vector<int> usedGroups;
    if ((quality == ItemQuality::Rare || quality == ItemQuality::Crafted)) {
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
            rollRandom(result.randomState);
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
        const int preferred = prefix ? preferredPrefix : preferredSuffix;
        // D2MOO checks the item's class restriction, not the buyer/killer's class.
        auto roll = rollMagicAffix(records, std::span<const std::string>(item.equipment.types), item.equipment.requiredClass,
                                   affixLevel, magicLevel, quality != ItemQuality::Magic, socketable,
                                   std::span<const int>(usedGroups), force,
                                   result.randomState, preferred);
        result.randomState = roll.randomState;
        if (!roll.row) return false;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return record.row == *roll.row; });
        if (found == records.end())
            return false;
        std::vector<int32_t> values;
        if (quality == ItemQuality::Magic) {
            auto properties = rollDisplayProperties(found->properties, result.randomState);
            result.randomState = properties.randomState;
            values = std::move(properties.values);
        }
        result.generation.affixes.push_back({prefix, int32_t(found->row), std::move(values)});
        result.generation.requiredLevel = std::max(result.generation.requiredLevel,
                                                    found->requiredLevel);
        usedGroups.push_back(found->group);
        return true;
    };
    if (quality == ItemQuality::Magic) {
        const bool prefix = preferredPrefix >= 0 && append(true, preferredPrefix > 0);
        if (!prefix || preferredSuffix >= 0)
            append(false, !prefix || preferredSuffix > 0);
    } else {
        auto below = [&](uint32_t limit) {
            rollRandom(result.randomState);
            return uint32_t(result.randomState) % limit;
        };
        int target;
        if (quality == ItemQuality::Crafted) {
            const int minimum = itemLevel > 70 ? 4 : itemLevel > 50 ? 3 : itemLevel > 30 ? 2 : 1;
            target = std::max(minimum, int(below(5)));
        } else {
            constexpr int counts[]{3, 4, 4, 5, 5, 5, 6, 6};
            target = item.equipment.isType("jewl") ? 3 + int(below(2)) : counts[below(8)];
        }
        int prefixes = 0, suffixes = 0;
        bool prefixesExhausted = false, suffixesExhausted = false;
        for (int attempt = 0; attempt < target && (!prefixesExhausted || !suffixesExhausted);) {
            const bool choosePrefix = quality == ItemQuality::Crafted
                ? ((below(2) == 0 || suffixes >= 3) && prefixes < 3)
                : (suffixesExhausted || (!prefixesExhausted && below(2) == 0));
            if (append(choosePrefix, true)) {
                (choosePrefix ? prefixes : suffixes)++;
                ++attempt;
            } else if (quality == ItemQuality::Crafted) {
                ++attempt;
            } else {
                (choosePrefix ? prefixesExhausted : suffixesExhausted) = true;
            }
            prefixesExhausted = prefixesExhausted || prefixes >= 3;
            suffixesExhausted = suffixesExhausted || suffixes >= 3;
        }
        for (int slot = 0; slot < 3; ++slot) {
            for (bool prefix : {true, false}) {
                int current = 0;
                for (auto &affix : result.generation.affixes) {
                    if (affix.prefix != prefix || current++ != slot) continue;
                    const auto &records = prefix ? data.magicPrefixes : data.magicSuffixes;
                    const auto found = std::find_if(records.begin(), records.end(),
                        [&](const auto &record) { return int32_t(record.row) == affix.row; });
                    auto properties = rollDisplayProperties(found->properties, result.randomState);
                    result.randomState = properties.randomState;
                    affix.propertyRolls = std::move(properties.values);
                    break;
                }
            }
        }
    }
    if (quality == ItemQuality::Crafted)
        result.generation.requiredLevel = std::min(98, std::max(result.generation.requiredLevel,
            item.base.requiredLevel.value_or(0)) + 10 + 3 * int(result.generation.affixes.size()));
    if (result.generation.affixes.empty())
        result.deferred = "No eligible original affix: " + item.code;
    return result;
}
} // namespace d2x
