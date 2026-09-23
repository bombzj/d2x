#include "item_grades.hpp"
#include "item_properties.hpp"
#include "gameplay/loot/special.hpp"
#include <algorithm>
#include <utility>

namespace d2x {
void loadItemGrades(ClassicData &data) {
    if (auto found = data.tables.find("qualityitems"); found != data.tables.end()) {
        const auto &table = found->second;
        for (size_t row = 0; row < table.rows().size(); ++row) {
            QualityGradeRecord grade;
            grade.row = row;
            grade.name = table.value(row, "effect1");
            grade.weapon = table.number(row, "weapon").value_or(0) != 0;
            grade.armor = table.number(row, "armor").value_or(0) != 0;
            for (auto [column, type] : {std::pair{"shield", "shld"}, {"thrown", "thro"},
                                        {"scepter", "scep"}, {"wand", "wand"},
                                        {"staff", "staf"}, {"bow", "bow"},
                                        {"boots", "boot"}, {"gloves", "glov"}, {"belt", "belt"}})
                if (table.number(row, column).value_or(0))
                    grade.specificTypes.emplace_back(type);
            if (table.number(row, "bow").value_or(0))
                grade.specificTypes.emplace_back("xbow");
            for (int slot = 1; slot <= 2; ++slot) {
                auto suffix = std::to_string(slot);
                auto code = table.value(row, "mod" + suffix + "code");
                if (!code.empty())
                    grade.properties.push_back({std::string(code),
                                                std::string(table.value(row, "mod" + suffix + "param")),
                                                table.number(row, "mod" + suffix + "min"),
                                                table.number(row, "mod" + suffix + "max"),
                                                isDirectPropertyRoll(data, code)});
            }
            data.superiorGrades.push_back(std::move(grade));
        }
    }
    if (auto found = data.tables.find("lowqualityitems"); found != data.tables.end()) {
        const auto &table = found->second;
        for (size_t row = 0; row < table.rows().size(); ++row)
            if (auto name = table.value(row, "Name"); !name.empty()) {
                QualityGradeRecord grade;
                grade.row = row;
                grade.name = name;
                data.inferiorGrades.push_back(std::move(grade));
            }
    }
}
bool isGradeEligible(const QualityGradeRecord &record, const ItemDefinition &item) {
    bool special = false;
    for (auto type : {"shld", "scep", "wand", "staf", "bow", "xbow", "boot", "glov", "belt"})
        special |= item.base.type == type;
    bool allowed = !special && ((record.weapon && item.family == ItemFamily::Weapon) ||
                                (record.armor && item.family == ItemFamily::Armor));
    allowed |= std::any_of(record.specificTypes.begin(), record.specificTypes.end(),
                           [&](const auto &type) { return item.equipment.isType(type); });
    return allowed;
}
GradeGenerationResult rollItemGrade(const ClassicData &data, const ItemDefinition &item,
                                    ItemQuality quality, uint64_t seed) {
    GradeGenerationResult result;
    result.randomState = seed;
    result.generation.quality = quality;
    if (item.family != ItemFamily::Weapon && item.family != ItemFamily::Armor) {
        result.deferred = "Unsupported grade item base: " + item.code;
        return result;
    }
    std::vector<const QualityGradeRecord *> candidates;
    const auto &records = quality == ItemQuality::Superior ? data.superiorGrades : data.inferiorGrades;
    for (const auto &record : records) {
        if (quality == ItemQuality::Inferior) {
            candidates.push_back(&record);
            continue;
        }
        if ((item.equipment.throwable || item.maxDurability == 0) && record.row >= 4)
            continue;
        if (isGradeEligible(record, item))
            candidates.push_back(&record);
    }
    if (candidates.empty()) {
        result.deferred = "No original grade row for " + item.code;
        return result;
    }
    result.randomState = uint64_t(uint32_t(result.randomState)) * 0x6ac690c5ULL +
                         (result.randomState >> 32);
    const auto *selected = candidates[uint32_t(result.randomState) % candidates.size()];
    result.generation.gradeRow = int32_t(selected->row);
    if (quality == ItemQuality::Superior) {
        auto rolls = rollDisplayProperties(selected->properties, result.randomState);
        result.randomState = rolls.randomState;
        result.generation.propertyRolls = std::move(rolls.values);
    }
    return result;
}
} // namespace d2x
