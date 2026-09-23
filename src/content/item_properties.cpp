#include "item_properties.hpp"
#include <algorithm>
#include <stdexcept>

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
            if (!name.empty())
                data.itemStats.push_back({row, std::string(name), table.number(row, "ID")});
        }
    }
}
bool isDirectPropertyRoll(const ClassicData &data, std::string_view code) {
    auto found = std::find_if(data.properties.begin(), data.properties.end(),
                              [&](const auto &property) { return property.code == code; });
    return found != data.properties.end() && found->operations.size() == 1 &&
           found->operations.front().function == 1 && !found->operations.front().stat.empty();
}
} // namespace d2x
