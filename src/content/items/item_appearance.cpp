#include "item_appearance.hpp"
#include "content/classic_data.hpp"
#include "gameplay/items/state.hpp"
#include <string_view>

namespace d2x {
std::array<int, 2> inventoryColorTransform(const ClassicData &content, const ItemCatalog &catalog,
                                          const ItemInstance &item) {
    const auto *definition = catalog.find(item.definition);
    if (!definition) return {};
    const auto &base = content.tables.at(definition->base.sourceTable);
    const int group = base.number(definition->base.sourceRow, "InvTrans").value_or(0);
    auto transform = [&](int color) -> std::array<int, 2> {
        return group > 0 && group < 9 && color >= 0 && color < 21
            ? std::array{group, color} : std::array<int, 2>{};
    };
    auto colorCode = [&](std::string_view code) {
        const auto &colors = content.tables.at("colors");
        if (!code.empty())
            for (size_t row = 0; row < colors.rows().size(); ++row)
                if (colors.value(row, "Code") == code) return int(row);
        return -1;
    };
    // D2Common 1.13c ordinal 10893 / RVA 251C0. Suffixes precede
    // prefixes; crafted/tempered items use the default (gem/auto) branch.
    if (item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare) {
        for (bool prefix : {false, true})
            for (const auto &affix : item.affixes) {
                if (affix.prefix != prefix || affix.row < 0) continue;
                const auto &table = content.tables.at(prefix ? "magicprefix" : "magicsuffix");
                if (size_t(affix.row) >= table.rows().size()) continue;
                const int color = colorCode(table.value(size_t(affix.row), "transformcolor"));
                if (color >= 0) return transform(color);
            }
    } else if (item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set) {
        if (item.specialRow < 0) return {};
        const auto &table = content.tables.at(item.quality == ItemQuality::Unique ? "uniqueitems" : "setitems");
        if (size_t(item.specialRow) >= table.rows().size()) return {};
        return transform(colorCode(table.value(size_t(item.specialRow), "invtransform")));
    } else if ((item.nativeFlags & 0x800u) && !item.socketedItems.empty()) {
        // Only the first inventory node is inspected, even if it is a rune/jewel.
        const auto &first = item.socketedItems.front();
        const auto *gem = catalog.find(first.definition);
        if (gem && gem->equipment.isType("gem")) {
            const auto &table = content.tables.at("gems");
            for (size_t row = 0; row < table.rows().size(); ++row)
                if (table.value(row, "code") == first.definition)
                    return transform(table.number(row, "transform").value_or(-1));
        }
    }
    if (item.nativeAutoAffix) {
        const auto &table = content.tables.at("automagic");
        if (item.nativeAutoAffix > table.rows().size()) return {};
        return transform(colorCode(table.value(item.nativeAutoAffix - 1, "transformcolor")));
    }
    return {};
}
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
