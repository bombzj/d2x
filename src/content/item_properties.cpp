#include "item_properties.hpp"
#include <algorithm>
#include <stdexcept>
#include <charconv>

namespace d2x {
void loadPropertyData(ClassicData &data) {
    if (data.profile != "lod-named-txt-v1")
        return;
    auto properties = data.tables.find("properties");
    if (properties != data.tables.end()) {
        const auto &table = properties->second;
        for (auto column : {"code", "func1", "stat1", "set1", "val1"})
            if (!table.has(column))
                throw std::runtime_error("Properties lacks field: " + std::string(column));
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto code = table.value(row, "code");
            if (code.empty())
                continue;
            PropertyDefinition definition;
            definition.row = row;
            definition.code = code;
            for (int slot = 1; slot <= 7; ++slot) {
                auto suffix = std::to_string(slot);
                auto function = table.number(row, "func" + suffix);
                if (!function)
                    continue;
                if (*function < 0 || *function > 255)
                    throw std::runtime_error("Invalid original property function: " + definition.code);
                definition.operations.push_back({*function, std::string(table.value(row, "stat" + suffix)),
                                                 std::string(table.value(row, "set" + suffix)),
                                                 std::string(table.value(row, "val" + suffix))});
            }
            data.properties.push_back(std::move(definition));
        }
    }
    auto stats = data.tables.find("itemstatcost");
    if (stats != data.tables.end()) {
        const auto &table = stats->second;
        if (!table.has("Stat") || !table.has("ID"))
            throw std::runtime_error("ItemStatCost lacks Stat or ID");
        for (size_t row = 0; row < table.rows().size(); ++row) {
            auto name = table.value(row, "Stat");
            if (!name.empty()) {
                ItemStatDefinition stat{row, std::string(name), table.number(row, "ID")};
                stat.descriptionPriority = table.number(row, "descpriority").value_or(0);
                stat.descriptionFunction = table.number(row, "descfunc").value_or(0);
                stat.descriptionValue = table.number(row, "descval").value_or(0);
                stat.positive = table.value(row, "descstrpos");
                stat.negative = table.value(row, "descstrneg");
                stat.suffix = table.value(row, "descstr2");
                stat.operation = table.number(row, "op").value_or(0);
                stat.operationParameter = table.number(row, "op param").value_or(0);
                stat.operationBase = table.value(row, "op base");
                stat.operationStat = table.value(row, "op stat1");
                data.itemStats.push_back(std::move(stat));
            }
        }
    }
}
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code) {
    auto found = std::find_if(data.properties.begin(), data.properties.end(),
                              [&](const auto &property) { return property.code == code; });
    if (found == data.properties.end() || found->operations.empty()) return false;
    switch (found->operations.front().function) {
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
    case 9: case 10: case 21: case 22: return true;
    default: return false;
    }
}
std::vector<ResolvedItemStat> resolvePropertyStats(const ClassicData &data,
    const PropertyRange &property, int roll, int level) {
    std::vector<ResolvedItemStat> result;
    const auto found = std::find_if(data.properties.begin(), data.properties.end(),
        [&](const auto &p) { return p.code == property.code; });
    if (found == data.properties.end()) return result;
    auto number = [](std::string_view text) {
        int value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size() ? value : 0;
    };
    int parameter = number(property.parameter);
    if (!property.parameter.empty() && !parameter)
        if (auto skills = data.tables.find("skills"); skills != data.tables.end())
            for (size_t row = 0; row < skills->second.rows().size(); ++row)
                if (skills->second.value(row, "skill") == property.parameter)
                    parameter = skills->second.number(row, "Id").value_or(0);
    auto append = [&](std::string stat, int value, int layer = 0) {
        const int rawValue = value;
        auto definition = std::find_if(data.itemStats.begin(), data.itemStats.end(),
            [&](const auto &s) { return s.name == stat; });
        if (definition == data.itemStats.end()) return;
        std::string effect = stat;
        if (definition->operationBase == "level" &&
            (definition->operation == 2 || definition->operation == 4 || definition->operation == 5)) {
            value = int(int64_t(value) * std::clamp(level, 1, 99) /
                        (int64_t(1) << std::clamp(definition->operationParameter, 0, 30)));
            effect = definition->operationStat;
            if (definition->operation == 5) {
                if (effect == "armorclass") effect = "item_armor_percent";
                else if (effect == "maxdamage") effect = "item_maxdamage_percent";
                else if (effect == "mindamage") effect = "item_mindamage_percent";
            }
        }
        result.push_back({std::move(stat), std::move(effect), value, layer, rawValue});
    };
    for (const auto &op : found->operations) {
        switch (op.function) {
        case 1: case 2: case 3: case 4: case 8:
            append(op.stat, roll); break;
        case 9: append(op.stat, roll, parameter); break;
        case 5: append("mindamage", roll); break;
        case 6: append("maxdamage", roll); break;
        case 7:
            append("item_mindamage_percent", roll);
            append("item_maxdamage_percent", roll); break;
        case 15: append(op.stat, property.minimum.value_or(0)); break;
        case 16: append(op.stat, property.maximum.value_or(0)); break;
        case 17: append(op.stat, parameter); break;
        case 20: append("item_indesctructible", 1); break;
        case 10: append(op.stat, roll, parameter % 3 + 8 * (parameter / 3)); break;
        case 21: append(op.stat, roll, number(op.value)); break;
        case 22: append(op.stat, roll, parameter); break;
        default: break; // Other handlers require their own verified item/state semantics.
        }
    }
    return result;
}
std::vector<ResolvedItemStat> resolveItemStats(const ClassicData &data,
    const ItemInstance &item, int level) {
    std::vector<ResolvedItemStat> result;
    if (!item.identified) return result;
    if (item.nativeProperties) {
        for (const auto &saved : item.savedStats) {
            auto definition = std::find_if(data.itemStats.begin(), data.itemStats.end(),
                [&](const auto &entry) { return entry.id == saved.id; });
            if (definition == data.itemStats.end()) throw std::runtime_error("Unknown saved item stat");
            int value = saved.value;
            std::string effect = definition->name;
            if (definition->operationBase == "level" &&
                (definition->operation == 2 || definition->operation == 4 || definition->operation == 5)) {
                value = int(int64_t(value) * std::clamp(level, 1, 99) /
                    (int64_t(1) << std::clamp(definition->operationParameter, 0, 30)));
                effect = definition->operationStat;
                if (definition->operation == 5) {
                    if (effect == "armorclass") effect = "item_armor_percent";
                    else if (effect == "maxdamage") effect = "item_maxdamage_percent";
                    else if (effect == "mindamage") effect = "item_mindamage_percent";
                }
            }
            result.push_back({definition->name, std::move(effect), value, saved.parameter, saved.value});
        }
        return result;
    }
    auto append = [&](const auto &properties, const auto &rolls) {
        if (properties.size() != rolls.size()) throw std::runtime_error("Item property roll count mismatch");
        for (size_t i = 0; i < properties.size(); ++i) {
            auto stats = resolvePropertyStats(data, properties[i], rolls[i], level);
            result.insert(result.end(), stats.begin(), stats.end());
        }
    };
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
        for (const auto &r : records)
            if (int32_t(r.row) == item.specialRow) { append(r.properties, item.propertyRolls); break; }
    }
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? data.magicPrefixes : data.magicSuffixes;
        for (const auto &r : records)
            if (int32_t(r.row) == affix.row) { append(r.properties, affix.propertyRolls); break; }
    }
    if (item.quality == ItemQuality::Superior)
        for (const auto &r : data.superiorGrades)
            if (int32_t(r.row) == item.gradeRow) { append(r.properties, item.propertyRolls); break; }
    return result;
}
} // namespace d2x
