#include "gameplay/session/session.hpp"
#include "content/item_grades.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace d2x {
void GameSession::validateItemProperties(const SessionSnapshot &snapshot) const {
    auto requireItem = [](bool condition, const char *reason) {
        if (!condition)
            throw std::runtime_error(std::string("Invalid save item: ") + reason);
    };
    auto validRoll = [](const PropertyRange &property, int value) {
        return property.directRoll && property.minimum && property.maximum &&
                       *property.maximum > *property.minimum
                   ? value >= *property.minimum && value <= *property.maximum
                   : value == property.minimum.value_or(0);
    };
    std::set<size_t> uniqueRows;
    for (const auto &[id, item] : snapshot.inventory.items) {
        if (item.quality == ItemQuality::Normal) {
            requireItem(item.specialRow == -1 && item.gradeRow == -1 && item.requiredLevel == 0 &&
                            item.rarePrefixRow == -1 && item.rareSuffixRow == -1 &&
                            item.propertyRolls.empty() && item.affixes.empty(),
                        "normal properties");
            continue;
        }
        if (item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare) {
            requireItem(item.specialRow == -1 && item.gradeRow == -1 && item.propertyRolls.empty() &&
                            !item.affixes.empty() &&
                            item.affixes.size() <= (item.quality == ItemQuality::Magic ? 2u : 6u),
                        "affix parameters");
            std::set<int> groups;
            int prefixes = 0, suffixes = 0, requiredLevel = 0;
            const auto *definition = content_.items.find(item.definition);
            if (item.quality == ItemQuality::Rare) {
                auto checkName = [&](const auto &records, int32_t row) {
                    auto found = std::find_if(records.begin(), records.end(),
                                              [&](const auto &record) { return int32_t(record.row) == row; });
                    if (found == records.end()) return false;
                    auto matches = [&](const std::string &type) { return definition->equipment.isType(type); };
                    return std::any_of(found->includedTypes.begin(), found->includedTypes.end(), matches) &&
                           !std::any_of(found->excludedTypes.begin(), found->excludedTypes.end(), matches);
                };
                requireItem(checkName(content_.rarePrefixes, item.rarePrefixRow) &&
                                checkName(content_.rareSuffixes, item.rareSuffixRow),
                            "rare name rows");
            } else
                requireItem(item.rarePrefixRow == -1 && item.rareSuffixRow == -1,
                            "magic item rare names");
            int affixLevel = itemAffixLevel(int(item.level), definition->base.level.value_or(0),
                                           definition->base.magicLevel.value_or(0));
            for (const auto &affix : item.affixes) {
                const auto &records = affix.prefix ? content_.magicPrefixes : content_.magicSuffixes;
                auto found = std::find_if(records.begin(), records.end(),
                                          [&](const auto &record) { return int32_t(record.row) == affix.row; });
                requireItem(found != records.end() &&
                                found->properties.size() == affix.propertyRolls.size() &&
                                groups.insert(found->group).second,
                            "affix row or group");
                auto matches = [&](const std::string &type) { return definition->equipment.isType(type); };
                requireItem(found->level <= affixLevel &&
                                (!found->maxLevel || affixLevel <= found->maxLevel) &&
                                (item.quality != ItemQuality::Rare || found->rareAllowed) &&
                                (found->characterClass.empty() || found->characterClass == "bar") &&
                                std::any_of(found->includedTypes.begin(), found->includedTypes.end(), matches) &&
                                !std::any_of(found->excludedTypes.begin(), found->excludedTypes.end(), matches),
                            "affix eligibility");
                for (size_t index = 0; index < affix.propertyRolls.size(); ++index)
                    requireItem(validRoll(found->properties[index], affix.propertyRolls[index]),
                                "affix property roll");
                requiredLevel = std::max(requiredLevel, found->requiredLevel);
                (affix.prefix ? prefixes : suffixes)++;
            }
            requireItem(prefixes <= 3 && suffixes <= 3 && item.requiredLevel == requiredLevel,
                        "affix slots or level requirement");
            continue;
        }
        if (item.quality == ItemQuality::Superior || item.quality == ItemQuality::Inferior) {
            const auto &records = item.quality == ItemQuality::Superior ? content_.superiorGrades
                                                                         : content_.inferiorGrades;
            auto found = std::find_if(records.begin(), records.end(),
                                      [&](const auto &record) { return int32_t(record.row) == item.gradeRow; });
            const auto *definition = content_.items.find(item.definition);
            requireItem(found != records.end() && item.specialRow == -1 && item.requiredLevel == 0 &&
                            item.rarePrefixRow == -1 && item.rareSuffixRow == -1 &&
                            item.affixes.empty() &&
                            (item.quality != ItemQuality::Superior ||
                             ((!definition->equipment.throwable && definition->maxDurability != 0) ||
                              found->row < 4)) &&
                            (item.quality == ItemQuality::Inferior || isGradeEligible(*found, *definition)) &&
                            found->properties.size() == item.propertyRolls.size(),
                        "grade row");
            for (size_t index = 0; index < item.propertyRolls.size(); ++index)
                requireItem(validRoll(found->properties[index], item.propertyRolls[index]),
                            "grade property roll");
            continue;
        }
        requireItem(item.gradeRow == -1 && item.affixes.empty() &&
                        item.rarePrefixRow == -1 && item.rareSuffixRow == -1,
                    "special grade, affixes or rare name");
        const auto &records = item.quality == ItemQuality::Unique ? content_.uniqueItems : content_.setItems;
        auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto &record) { return int32_t(record.row) == item.specialRow; });
        requireItem(found != records.end() && found->code == item.definition &&
                        found->requiredLevel == item.requiredLevel &&
                        found->properties.size() == item.propertyRolls.size(),
                    "special row");
        for (size_t index = 0; index < item.propertyRolls.size(); ++index)
            requireItem(validRoll(found->properties[index], item.propertyRolls[index]),
                        "special property roll");
        if (item.quality == ItemQuality::Unique && !found->noLimit)
            requireItem(uniqueRows.insert(found->row).second &&
                            snapshot.loot.usedUniques.contains(uint32_t(found->row)),
                        "duplicate or untracked unique");
    }
}
} // namespace d2x
