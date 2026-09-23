#include "item_quality.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
ItemQualityRules loadItemQualityRules(const ClassicData &data, const DataTable &ratios,
                                     std::string_view code) {
    if (data.profile != "lod-named-txt-v1")
        throw std::runtime_error("Item quality requires the LoD named table profile");
    const auto *item = data.items.find(code);
    if (!item || !item->base.level)
        throw std::runtime_error("Unknown item or missing base level: " + std::string(code));
    const auto &source = data.tables.at(item->base.sourceTable);
    const auto &types = data.tables.at("itemtypes");
    size_t typeRow = 0;
    for (; typeRow < types.rows().size(); ++typeRow)
        if (types.value(typeRow, "Code") == item->base.type)
            break;
    if (typeRow == types.rows().size())
        throw std::runtime_error("Missing primary item type");
    for (auto column : {"Normal", "Magic", "Rare", "Class"})
        if (!types.has(column))
            throw std::runtime_error("Missing item quality type column: " + std::string(column));
    ItemQualityRules result;
    result.baseLevel = *item->base.level;
    result.normalOnly = types.number(typeRow, "Normal").value_or(0) != 0;
    result.magicOnly = types.number(typeRow, "Magic").value_or(0) != 0;
    result.rareAllowed = types.number(typeRow, "Rare").value_or(0) != 0;
    const bool quest = source.number(item->base.sourceRow, "quest").value_or(0) != 0;
    result.uniqueOnly = source.number(item->base.sourceRow, "unique").value_or(0) ||
                        (result.magicOnly && quest);
    if (result.normalOnly || result.uniqueOnly)
        return result;
    const bool classSpecific = !types.value(typeRow, "Class").empty();
    const bool uber = (item->equipment.isType("armo") || item->equipment.isType("weap")) &&
                      !quest && item->base.type != "tpot" &&
                      (source.value(item->base.sourceRow, "ubercode") == code ||
                       source.value(item->base.sourceRow, "ultracode") == code);
    std::optional<size_t> selected;
    int version = -1;
    for (size_t row = 0; row < ratios.rows().size(); ++row) {
        auto rowVersion = ratios.number(row, "Version");
        auto rowUber = ratios.number(row, "Uber");
        auto rowClass = ratios.number(row, "Class Specific");
        if (rowVersion && rowUber && rowClass && *rowVersion >= version && *rowVersion <= 100 &&
            *rowUber == int(uber) && *rowClass == int(classSpecific)) {
            selected = row;
            version = *rowVersion;
        }
    }
    if (!selected)
        throw std::runtime_error("Missing matching ItemRatio record: " + std::string(code));
    const char *names[] = {"Unique", "Set", "Rare", "Magic", "HiQuality", "Normal"};
    for (size_t index = 0; index < result.ratios.size(); ++index) {
        std::string name = names[index];
        auto base = ratios.number(*selected, name);
        auto divisor = ratios.number(*selected, name + "Divisor");
        auto minimum = index < 4 ? ratios.number(*selected, name + "Min") : std::optional<int>{0};
        if (!base || !divisor || !minimum || *base < 0 || *divisor <= 0 || *minimum < 0)
            throw std::runtime_error("Invalid ItemRatio fields: " + name);
        result.ratios[index] = {*base, *divisor, *minimum};
    }
    return result;
}
} // namespace d2x
