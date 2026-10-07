#include "item_pricing.hpp"
#include "gameplay/items/state.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
std::optional<unsigned> itemTradePrice(const ClassicData &data, const ItemInstance &item,
                                       const VendorDefinition &vendor, bool repair,
                                       std::span<const int> questFactors, int reducedPrices,
                                       bool sale, int difficulty, unsigned autoAffix, std::optional<int64_t> bodyCost,
                                       std::span<const int> repairQuestFactors, std::optional<unsigned> bookRow) {
    if (sale && (repair || difficulty < 0 || difficulty >= int(vendor.maxBuy.size()))) return {};
    if (!repair && (item.nativeFlags & 0x20000u)) return 1;
    const auto *definition = data.items.find(item.definition);
    if (!definition || !definition->base.cost || *definition->base.cost < 0) return {};
    const bool specialQuality = item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted || item.quality == ItemQuality::Set || item.quality == ItemQuality::Unique || item.quality == ItemQuality::Tempered;
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
        if (item.nativeProperties) break;
        const auto &records = affix.prefix ? data.magicPrefixes : data.magicSuffixes;
        for (const auto &record : records)
            if (int32_t(record.row) == affix.row && !supported(record.properties)) return {};
    }
    if (!item.nativeProperties && item.specialRow >= 0) {
        const auto &records = item.quality == ItemQuality::Unique ? data.uniqueItems : data.setItems;
        for (const auto &record : records)
            if (int32_t(record.row) == item.specialRow && !supported(record.properties)) return {};
    }
    for (const auto &child : item.socketedItems) {
        const auto gem = data.socketGems.find(child.definition);
        if (!item.nativeProperties || !child.savedStats.empty() || gem == data.socketGems.end()) continue;
        if (definition->gemApplyType < 0 || definition->gemApplyType > 2) return {};
        for (const auto &property : gem->second.properties[size_t(definition->gemApplyType)])
            if (property.directRoll && property.minimum != property.maximum) return {};
    }
    const auto stats = resolveItemStats(data, item, 1);
    const auto &costs = data.tables.at("itemstatcost");
    for (const auto &stat : stats) {
        const auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(), [&](const auto &entry) { return entry.name == stat.name; });
        if (found == data.itemStats.end()) return {};
        const int encode = costs.number(found->row, "Encode").value_or(0);
        if (encode < 0 || encode > 4) return {};
    }
    auto statValue = [&](std::string_view name) {
        int result = 0;
        for (const auto &stat : stats) if (stat.name == name) result += stat.rawValue;
        return result;
    };
    const int stack = definition->maxStack > 1
        ? std::clamp(int(definition->maxStack) + statValue("item_extra_stack"), 1, 511) : 1;
    const int maxDurability = int(itemMaximumDurability(data, item, stats));
    bool missingCharges = false;
    for (const auto &stat : stats) {
        if (stat.name != "item_charged_skill") continue;
        if (stat.rawValue < 0 || stat.rawValue > 65535) return {};
        const unsigned current = unsigned(stat.rawValue) & 0xff, maximum = unsigned(stat.rawValue) >> 8;
        if (current > maximum) return {};
        missingCharges |= current < maximum;
    }
    const auto &source = data.tables.at(definition->base.sourceTable);
    const bool durable = !source.number(definition->base.sourceRow, "nodurability").value_or(0) &&
        definition->maxDurability > 0 && maxDurability > 0 && !statValue("item_indesctructible");
    const bool refillable = definition->equipment.throwable && definition->maxStack > 1;
    const bool repairable = item.identified && !(item.nativeFlags & 0x400000u) &&
        (missingCharges || (definition->equipment.repairable && (durable || refillable)));
    if (repair && !repairable) return 0;
    if (item.nativeFlags & 0x20000u) return 1;
    if (definition->equipment.isType("body") && !bodyCost) return {};
    const bool quiver = !definition->equipment.quiver.empty();
    int chargePrice = int(definition->bookChargeCost);
    if (definition->bookCapacity && item.nativeProperties) {
        const auto &books = data.tables.at("books");
        if (!bookRow || *bookRow >= books.rows().size() || books.value(*bookRow, "BookSpellCode") != definition->code) return {};
        const auto price = books.number(*bookRow, "CostPerCharge");
        if (!price || *price < 0) return {};
        chargePrice = *price;
    }
    int skillShift = costs.number(0, "stuff").value_or(6);
    if (skillShift <= 0 || skillShift > 8) skillShift = 6;
    const unsigned skillMask = (1u << skillShift) - 1;
    std::map<std::pair<std::string, int>, int> bonuses;
    for (const auto &stat : stats) bonuses[{stat.name, stat.layer}] += stat.rawValue;
    auto skillRow = [&](int skill) -> std::optional<size_t> {
        const auto &skills = data.tables.at("skills");
        for (size_t row = 0; row < skills.rows().size(); ++row)
            if (skills.number(row, "Id") == skill) return row;
        return {};
    };
    auto calculateBase = [&](bool saleChannel, bool repairChannel = false) -> std::optional<int64_t> {
    int64_t base = bodyCost.value_or(*definition->base.cost);
    if (definition->family == ItemFamily::Armor && definition->base.maxDefense.value_or(0) > 0)
        base = int64_t(item.defense) * base / *definition->base.maxDefense;
    if (definition->bookCapacity) base += int64_t(item.charges) * chargePrice;
    if (quiver) base = int64_t(repairChannel ? unsigned(stack) : std::max(1u, item.quantity)) * base / 1024;
    const int divisor = definition->maxStack > 1 && !quiver && !definition->bookCapacity ? stack : 1;
    int64_t buyBase = base;
    auto priceMultiplier = [](int64_t amount, int factor, int denominator, bool large) {
        return large && factor ? amount / denominator * factor : amount * factor / denominator;
    };
    auto addStaffCost = [&]() {
        int64_t extra = 0;
        int64_t buyExtra = 0;
        const auto &types = data.tables.at("itemtypes");
        bool staffMods = false;
        for (size_t row = 0; row < types.rows().size(); ++row)
            if (types.value(row, "Code") == definition->base.type) staffMods = !types.value(row, "StaffMods").empty();
        if (!staffMods) return true;
        const auto &skills = data.tables.at("skills");
        std::map<int, int64_t> skillBonuses;
        for (const auto &stat : stats) if (stat.name == "item_singleskill") skillBonuses[stat.layer] += stat.rawValue;
        for (const auto &[skill, value] : skillBonuses) {
            bool found = false;
            for (size_t index = 0; index < skills.rows().size(); ++index) {
                if (skills.number(index, "Id") != skill) continue;
                found = true;
                const int add = skills.number(index, "cost add").value_or(0);
                const int mult = skills.number(index, "cost mult").value_or(0);
                extra += (2 * value - 1) * (add + priceMultiplier(base, mult, saleChannel ? 4096 : 1024, buyBase > 65535));
                buyExtra += (2 * value - 1) * (add + priceMultiplier(buyBase, mult, 1024, buyBase > 65535));
                break;
            }
            if (!found) return false;
        }
        base += extra / divisor;
        buyBase += buyExtra / divisor;
        return true;
    };
    if (!specialQuality && !addStaffCost()) return {};
    int64_t affixCost = 0;
    int64_t buyAffixCost = 0;
    auto addRow = [&](const DataTable &table, size_t row, const char *add, const char *multiply) {
        affixCost += table.number(row, add).value_or(0) +
            priceMultiplier(base, table.number(row, multiply).value_or(0), 1024, buyBase > 65535);
        buyAffixCost += table.number(row, add).value_or(0) +
            priceMultiplier(buyBase, table.number(row, multiply).value_or(0), 1024, buyBase > 65535);
    };
    if (item.identified) {
        if (autoAffix) {
            size_t row = autoAffix - 1;
            bool found = false;
            for (const auto *name : {"magicsuffix", "magicprefix", "automagic"}) {
                const auto table = data.tables.find(name);
                if (table == data.tables.end()) return {};
                if (row < table->second.rows().size()) {
                    addRow(table->second, row, "add", "multiply");
                    found = true;
                    break;
                }
                row -= table->second.rows().size();
            }
            if (!found) return {};
        }
        for (const auto &affix : item.affixes) {
            if (affix.row < 0 || size_t(affix.row) >= data.tables.at(affix.prefix ? "magicprefix" : "magicsuffix").rows().size()) return {};
            addRow(data.tables.at(affix.prefix ? "magicprefix" : "magicsuffix"),
                   size_t(affix.row), "add", "multiply");
        }
        if ((item.quality == ItemQuality::Unique || item.quality == ItemQuality::Set) && item.specialRow < 0) return {};
        if (item.specialRow >= 0) {
            if (size_t(item.specialRow) >= data.tables.at(item.quality == ItemQuality::Unique ? "uniqueitems" : "setitems").rows().size()) return {};
            addRow(data.tables.at(item.quality == ItemQuality::Unique ? "uniqueitems" : "setitems"),
                   size_t(item.specialRow), "cost add", "cost mult");
        }
    }
    // D2Common ITEMS_CalculateAdditionalCostsForBonusStats aggregates each
    // stat/layer once, in the original unscaled ItemStatCost value domain.
    int64_t bonusCost = 0;
    int64_t buyBonusCost = 0;
    if (item.identified && (item.quality == ItemQuality::Magic || item.quality == ItemQuality::Rare || item.quality == ItemQuality::Crafted || item.quality == ItemQuality::Superior || item.quality == ItemQuality::Tempered))
        for (const auto &[key, value] : bonuses) {
            if (!value) continue;
            auto found = std::find_if(data.itemStats.begin(), data.itemStats.end(),
                [&](const auto &stat) { return stat.name == key.first; });
            if (found == data.itemStats.end()) return {};
            const int encode = costs.number(found->row, "Encode").value_or(0);
            if (encode >= 1 && encode <= 3) {
                const auto &skills = data.tables.at("skills");
                const auto row = skillRow(encode == 1 ? key.second : int(unsigned(key.second) >> skillShift));
                if (!row) return {};
                const int64_t level = encode == 1 ? value : int64_t(unsigned(key.second) & skillMask);
                const int add = skills.number(*row, "cost add").value_or(0);
                const int mult = skills.number(*row, "cost mult").value_or(0);
                bonusCost += add + priceMultiplier(base * level, mult, saleChannel ? 4096 : 1024, buyBase * level > 65535);
                buyBonusCost += add + priceMultiplier(buyBase * level, mult, 1024, buyBase * level > 65535);
            } else {
                const int64_t amount = encode == 4 ? (int64_t((unsigned(value) >> 2) & 0x3ff) + int64_t((unsigned(value) >> 12) & 0x3ff) - 512) / 2 : value;
                const int add = costs.number(found->row, "Add").value_or(0);
                const int mult = costs.number(found->row, "Multiply").value_or(0);
                bonusCost += add + priceMultiplier(base * amount, mult, 1024, buyBase * amount > 65535);
                buyBonusCost += add + priceMultiplier(buyBase * amount, mult, 1024, buyBase * amount > 65535);
            }
        }
    if (item.identified && item.quality == ItemQuality::Inferior) {
        affixCost = base / -2;
        buyAffixCost = buyBase / -2;
    }
    base += bonusCost / divisor + affixCost / divisor;
    buyBase += buyBonusCost / divisor + buyAffixCost / divisor;
    if (item.identified && specialQuality && !addStaffCost()) return {};
    for (const auto &child : item.socketedItems) {
        const auto *filler = data.items.find(child.definition);
        if (!filler || !filler->base.cost || *filler->base.cost < 0) return {};
        base += *filler->base.cost / 2;
    }
    if (saleChannel && (item.nativeFlags & 0x400000u)) base /= 4;
    if (saleChannel) {
        const auto &types = data.tables.at("itemtypes");
        for (size_t row = 0; row < types.rows().size(); ++row)
            if (types.value(row, "Code") == definition->base.type && !types.value(row, "Class").empty()) { base /= 4; break; }
        if ((item.nativeFlags & 0x400000u) && durable && !item.durability) base = 0;
    }
    return base;
    };
    const auto rawCost = calculateBase(sale, repair);
    if (!rawCost) return {};
    int64_t base = *rawCost;
    auto multiply = [](int64_t amount, int factor, int branchFactor) {
        return amount > 65535 && branchFactor ? amount / 1024 * factor : amount * factor / 1024;
    };
    auto adjusted = [&](int64_t amount, int factor, int branchFactor) {
        amount = multiply(amount, factor, branchFactor);
        for (int questFactor : questFactors) amount = multiply(amount, questFactor, questFactor);
        return amount;
    };
    if (repair) {
        int64_t chargeCost = 0;
        const auto &skills = data.tables.at("skills");
        for (const auto &[key, value] : bonuses) {
            if (key.first != "item_charged_skill") continue;
            const unsigned current = unsigned(value) & 0xff, maximum = unsigned(value) >> 8;
            if (current > maximum) return {};
            if (current == maximum) continue;
            const auto row = skillRow(int(unsigned(key.second) >> skillShift));
            if (!row || !maximum) return {};
            const int64_t chargeBase = 10000 * (int64_t(unsigned(key.second) & skillMask) + 2 * (skills.number(*row, "reqlevel").value_or(0) / 6) + 2);
            const int multiplier = skills.number(*row, "cost mult").value_or(0);
            chargeCost += (skills.number(*row, "cost add").value_or(0) + multiply(chargeBase, multiplier, multiplier)) * (maximum - current) / maximum;
        }
        if (refillable && definition->equipment.repairable) {
            const unsigned missing = item.quantity >= unsigned(stack) || statValue("item_replenish_quantity") ? 0 : unsigned(stack) - item.quantity;
            base = adjusted(base, vendor.repairMultiplier, vendor.repairMultiplier);
            base *= missing;
        } else if (durable && !quiver && !definition->equipment.throwable) {
            const int missing = item.durability >= unsigned(maxDurability) ? 0 : maxDurability - int(item.durability);
            const int repaired = missing && statValue("item_replenish_durability") ? (missing > 1 ? maxDurability - 1 : 0) : missing;
            base = base * repaired / maxDurability;
            base = adjusted(base, vendor.repairMultiplier, vendor.repairMultiplier);
        } else {
            base = adjusted(base, vendor.repairMultiplier, vendor.repairMultiplier);
        }
        base += chargeCost;
        const bool needsDurability = durable && item.durability < unsigned(maxDurability);
        const bool needsQuantity = refillable && definition->equipment.repairable && item.quantity < unsigned(stack);
        if (!missingCharges && !needsDurability && !needsQuantity) return 0;
    } else {
        base = adjusted(base, sale ? vendor.buyMultiplier : vendor.sellMultiplier, vendor.sellMultiplier);
        if (sale && refillable && repairable && !quiver) {
            const auto repairBase = calculateBase(false);
            if (!repairBase) return {};
            int64_t refillCost = multiply(*repairBase, vendor.repairMultiplier, vendor.repairMultiplier);
            for (int factor : repairQuestFactors) refillCost = multiply(refillCost, factor, factor);
            const unsigned missing = item.quantity >= unsigned(stack) || statValue("item_replenish_quantity") ? 0 : unsigned(stack) - item.quantity;
            base = base * stack - refillCost * missing;
        } else if (!quiver && !definition->bookCapacity) base *= std::max(1u, item.quantity);
        if (sale) base = std::min<int64_t>(base, vendor.maxBuy[size_t(difficulty)]);
    }
    if (!sale) base -= base * std::clamp(reducedPrices, 0, 99) / 100;
    return unsigned(std::clamp<int64_t>(base, 1, std::numeric_limits<unsigned>::max()));
}
std::optional<unsigned> itemGamblePrice(const ClassicData &data, std::string_view code, int level, unsigned format, int reducedPrices) {
    auto find = [&](std::string_view wanted) -> const ItemDefinition * { return data.items.find(wanted); };
    const auto *definition = find(code);
    if (!definition) return {};
    const auto &source = data.tables.at(definition->base.sourceTable);
    const auto normal = source.value(definition->base.sourceRow, "normcode");
    if (!normal.empty() && normal != "0") definition = find(normal);
    if (!definition) return {};
    const auto &table = data.tables.at(definition->base.sourceTable);
    const auto row = definition->base.sourceRow;
    const auto fixedCost = table.number(row, "gamble cost");
    if ((!format || definition->code == "rin" || definition->code == "amu") && !fixedCost) return {};
    int64_t amount = fixedCost.value_or(0);
    if (format && definition->code != "rin" && definition->code != "amu") {
        if (level < 1 || level > 99 || !definition->base.cost || !definition->base.level) return {};
        int64_t exceptional = 0, elite = 0, exceptionalCost = 0, eliteCost = 0;
        auto grade = [&](const char *column, int divisor, int64_t &chance, int64_t &cost) {
            const auto target = table.value(row, column);
            if (target.empty() || target == "0") return true;
            const auto *base = find(target);
            if (!base || !base->base.level || !base->base.cost) return false;
            chance = std::max<int64_t>(0, int64_t(100) * (level - *base->base.level) / divisor + 1);
            cost = *base->base.cost;
            return true;
        };
        if (!grade("ubercode", 2, exceptional, exceptionalCost) || !grade("ultracode", 4, elite, eliteCost)) return {};
        const int64_t quantity = std::max(1, (table.number(row, "minstack").value_or(0) + table.number(row, "maxstack").value_or(0)) / 2);
        const int64_t weighted = exceptional * exceptionalCost + elite * eliteCost + quantity * *definition->base.cost * (10000 - exceptional - elite);
        level = std::max(5, level);
        amount = ((2 * level + 1) / 3 + 20) * (weighted / 10000 + int64_t(250) * (level + std::max(0, *definition->base.level - 45) - *definition->base.level / 2) / 3) / 15;
    }
    if (format) amount -= amount * std::clamp(reducedPrices, 0, 99) / 100;
    if (amount < 0 || amount > std::numeric_limits<unsigned>::max()) return {};
    return unsigned(amount);
}
} // namespace d2x
