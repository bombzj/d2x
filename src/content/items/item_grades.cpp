#include "core/random.hpp"
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
                           [&](const auto &type) { return item.base.type==type; });
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
    const auto &records = quality == ItemQuality::Superior ? data.superiorGrades : data.inferiorGrades;
    const auto &table=data.tables.at(item.base.sourceTable);
    const size_t count=quality==ItemQuality::Superior && (item.equipment.throwable || table.number(item.base.sourceRow,"nodurability").value_or(0))
        ? std::min(size_t(4),records.size()):records.size();
    if (!count) {
        result.deferred = "No original grade row for " + item.code;
        return result;
    }
    std::vector<bool> checked(count);
    size_t remaining=count;
    const QualityGradeRecord *selected=nullptr;
    while(remaining) {
        const auto index=limitedRandom(result.randomState,unsigned(count));
        if(checked[index]) continue;
        if(quality==ItemQuality::Inferior || isGradeEligible(records[index],item)) {selected=&records[index];break;}
        checked[index]=true;--remaining;
    }
    if(!selected) {result.deferred="No original grade row for "+item.code;return result;}
    result.generation.gradeRow = int32_t(selected->row);
    if (quality == ItemQuality::Superior) {
        auto rolls = rollItemProperties(data,selected->properties, result.randomState,1);
        result.randomState = rolls.randomState;
        result.generation.propertyRolls = std::move(rolls.values);
        result.generation.operationRolls=std::move(rolls.operations);
    }
    result.generation.propertyRandom=result.randomState;
    return result;
}
} // namespace d2x
