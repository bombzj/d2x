#include "item_pricing.hpp"
#include "gameplay/items/state.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
std::optional<unsigned> itemTradePrice(const ClassicData &data, const ItemInstance &item,
                                       const VendorDefinition &vendor, bool repair,
                                       std::span<const int> questFactors, int reducedPrices,
                                       bool sale, int difficulty) {
    if (sale && (repair || difficulty < 0 || difficulty >= int(vendor.maxBuy.size()))) return {};
    const auto *definition = data.items.find(item.definition);
    if (!definition || !definition->base.cost || *definition->base.cost < 0) return {};
    auto identified = item;
    identified.identified = true;
    auto supported = [&](const auto &properties) {
        for (const auto &property : properties) {
            auto found = std::find_if(data.properties.begin(), data.properties.end(),
                [&](const auto &record) { return record.code == property.code; });
            if (found == data.properties.end()) return false;
            for (const auto &operation : found->operations)
                switch (operation.function) {
                case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
                case 9: case 10: case 14: case 15: case 16: case 17: case 20: case 21: case 22: case 23: case 24: break;
                default: return false;
                }
        }
        return true;
    };
    for (const auto &affix : item.affixes) {
        const auto &records = affix.prefix ? data.magicPrefixes : data.magicSuffixes;
        for (const auto &record : records)
            if (int32_t(record.row) == affix.row && !supported(record.properties)) return {};
    }
    if (item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
        for (const auto &record : records)
            if (int32_t(record.row) == item.specialRow && !supported(record.properties)) return {};
    }
    const auto stats = resolveItemStats(data, identified, 1);
    auto statValue = [&](std::string_view name) {
        int result = 0;
        for (const auto &stat : stats) if (stat.name == name) result += stat.rawValue;
        return result;
    };
    const int stack = definition->maxStack > 1
        ? std::clamp(int(definition->maxStack) + statValue("item_extra_stack"), 1, 511) : 1;
    const unsigned baseDurability = item.quality == ItemQuality::Inferior && definition->maxDurability
        ? std::max(1u, definition->maxDurability / 3) : definition->maxDurability;
    const int maxDurability = item.nativeProperties ? int(item.nativeMaxDurability) : baseDurability ? std::clamp(
        int(baseDurability) * (100 + statValue("item_maxdurability_percent")) / 100 +
        statValue("maxdurability"), 1, 255) : 0;
    if (repair && ((item.nativeFlags & 0x400000u) || !definition->equipment.repairable ||
        (!definition->equipment.throwable && statValue("item_indesctructible")))) return 0;
    int64_t base = *definition->base.cost;
    if (definition->family == ItemFamily::Armor && definition->base.maxDefense.value_or(0) > 0)
        base = int64_t(item.defense) * base / *definition->base.maxDefense;
    if (definition->bookCapacity) base += int64_t(item.charges) * definition->bookChargeCost;
    const bool quiver = !definition->equipment.quiver.empty();
    if (quiver) base = int64_t(item.quantity) * base / 1024;
    const int divisor = definition->maxStack > 1 && !quiver ? stack : 1;
    int64_t affixCost = 0;
    auto addRow = [&](const DataTable &table, size_t row, const char *add, const char *multiply) {
        affixCost += table.number(row, add).value_or(0) +
            base * table.number(row, multiply).value_or(0) / 1024;
    };
    if (item.identified) {
        for (const auto &affix : item.affixes)
            addRow(data.tables.at(affix.prefix ? "magicprefix" : "magicsuffix"),
                   size_t(affix.row), "add", "multiply");
        if (item.specialRow >= 0)
            addRow(data.tables.at(item.quality == ItemQuality::Unique ? "uniqueitems" : "setitems"),
                   size_t(item.specialRow), "cost add", "cost mult");
    }
    // D2Common ITEMS_CalculateAdditionalCostsForBonusStats aggregates each
    // stat/layer once, in the original unscaled ItemStatCost value domain.
    int64_t bonusCost = 0;
    std::map<std::pair<std::string, int>, int> bonuses;
    for (const auto &stat : stats) bonuses[{stat.name, stat.layer}] += stat.rawValue;
    const auto &costs = data.tables.at("itemstatcost");
    if (item.identified)
        for (const auto &[key, value] : bonuses) {
            if (!value) continue;
            auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(),
                [&](const auto &stat) { return stat.name == key.first; });
            if (found == data.itemStats.end()) return {};
            const int encode = costs.number(found->row, "Encode").value_or(0);
            if (encode == 1) {
                const auto &skills = data.tables.at("skills");
                for (size_t row = 0; row < skills.rows().size(); ++row)
                    if (skills.number(row, "Id") == key.second)
                        bonusCost += skills.number(row, "cost add").value_or(0) +
                            base * value * skills.number(row, "cost mult").value_or(0) / 1024;
            } else if (encode) return {}; // Trigger/charged/by-time prices need their own instance representation.
            else bonusCost += costs.number(found->row, "Add").value_or(0) +
                base * value * costs.number(found->row, "Multiply").value_or(0) / 1024;
        }
    if (item.quality == ItemQuality::Inferior) base /= 2;
    base += bonusCost / divisor + affixCost / divisor;
    for (const auto &child : item.socketedItems) {
        const auto *filler = data.items.find(child.definition);
        if (!filler || !filler->base.cost || *filler->base.cost < 0) return {};
        base += *filler->base.cost / 2;
    }
    if (repair) {
        if (definition->equipment.throwable && definition->maxStack > 1) {
            if (item.quantity >= unsigned(stack) || statValue("item_replenish_quantity")) return 0;
            base = base * vendor.repairMultiplier / 1024;
            for (int factor : questFactors) base = base * factor / 1024;
            base *= stack - int(item.quantity);
        } else {
            if (!maxDurability || item.durability >= unsigned(maxDurability)) return 0;
            int missing = maxDurability - int(item.durability);
            if (statValue("item_replenish_durability")) missing = std::max(0, missing - 1);
            if (!missing) return 0;
            base = base * missing / maxDurability * vendor.repairMultiplier / 1024;
            for (int factor : questFactors) base = base * factor / 1024;
        }
    } else {
        base = base * (sale ? vendor.buyMultiplier : vendor.sellMultiplier) / 1024;
        for (int factor : questFactors) base = base * factor / 1024;
        if (!quiver && !definition->bookCapacity)
            base *= sale ? item.quantity : std::max(1u, item.quantity);
        if (sale) base = std::min<int64_t>(base, vendor.maxBuy[size_t(difficulty)]);
    }
    if (!sale) base -= base * std::clamp(reducedPrices, 0, 99) / 100;
    return unsigned(std::clamp<int64_t>(base, 1, std::numeric_limits<unsigned>::max()));
}
} // namespace d2x
