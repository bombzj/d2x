#include "item_appearance.hpp"

namespace d2x {
void loadItemAppearances(std::vector<ItemDefinition> &items,
                         const std::map<std::string, DataTable, std::less<>> &tables,
                         const std::vector<std::string> &armorTypes) {
    for (auto &item : items) {
        const auto &source = tables.at(item.base.sourceTable);
        auto &appearance = item.appearance;
        appearance.component = source.number(item.base.sourceRow, "component").value_or(-1);
        appearance.token = source.value(item.base.sourceRow, "alternategfx");
        if (appearance.token.empty())
            appearance.token = source.value(item.base.sourceRow, "alternateGfx");
        if (item.base.sourceTable != "armor" || appearance.component != 1)
            continue;
        constexpr std::array<const char *, 6> columns{"rArm", "lArm", "Torso", "Legs",
                                                       "rSPad", "lSPad"};
        for (size_t index = 0; index < columns.size(); ++index) {
            auto row = source.number(item.base.sourceRow, columns[index]);
            if (row && *row >= 0 && size_t(*row) < armorTypes.size())
                appearance.body[index] = armorTypes[*row];
        }
    }
}
} // namespace d2x
