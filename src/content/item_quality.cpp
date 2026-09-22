#include "item_quality.hpp"
#include <algorithm>
#include <charconv>
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
LootPlan planConsumableLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                           int itemLevel, int upgradeLevel, uint64_t seed) {
    if (data.profile != "lod-named-txt-v1" || itemLevel < 1 || itemLevel > 99 ||
        upgradeLevel < 0 || upgradeLevel > 99)
        throw std::runtime_error("Unsupported consumable loot profile or level");
    LootPlan plan;
    plan.randomState = seed;
    auto roll = selectTreasure(data.treasures, root, seed, upgradeLevel,
        [&](const TreasureSelection &selection, uint64_t &random) {
            std::string_view code = selection.code;
            unsigned goldMultiplier = 256;
            if (code.size() >= 2 && code.front() == '"' && code.back() == '"')
                code = code.substr(1, code.size() - 2);
            if (code.starts_with("gld,mul=")) {
                auto value = code.substr(8);
                auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), goldMultiplier);
                if (error != std::errc{} || end != value.data() + value.size() || goldMultiplier > 32767) {
                    plan.deferred = "Unsupported gold multiplier: " + selection.code;
                    return false;
                }
                if (!goldMultiplier)
                    goldMultiplier = 256;
                code = "gld";
            }
            const auto *item = data.items.find(code);
            if (!item) {
                plan.deferred = "Unresolved TC token: " + selection.code;
                return false;
            }
            auto rules = loadItemQualityRules(data, ratios, item->code);
            auto quality = rollItemQuality(rules, itemLevel, 0, selection.quality, random);
            random = quality.randomState;
            const auto &source = data.tables.at(item->base.sourceTable);
            const bool gold = item->equipment.isType("gold");
            const bool quiver = !item->equipment.quiver.empty();
            const bool consumable = item->equipment.isType("poti") || item->equipment.isType("scro") ||
                                    gold || quiver;
            if (!rules.normalOnly || quality.quality != DropQuality::Normal ||
                item->family != ItemFamily::Misc || !consumable ||
                item->maxDurability || source.number(item->base.sourceRow, "quest").value_or(0)) {
                plan.deferred = "Unsupported instance: " + item->code + " requested=" +
                                dropQualityName(quality.quality);
                return false;
            }
            auto below = [&](unsigned bound) {
                if (!bound)
                    return 0u;
                random = uint64_t(uint32_t(random)) * 0x6ac690c5ULL + (random >> 32);
                return uint32_t(random) % bound;
            };
            unsigned quantity = 1;
            if (gold) {
                quantity = unsigned(itemLevel) + below(5 * unsigned(itemLevel));
                quantity = unsigned(uint64_t(quantity) * goldMultiplier / 256);
                if (!quantity || quantity > item->maxStack) {
                    plan.deferred = "Gold pile exceeds supported original stack limit";
                    return false;
                }
            }
            else if (item->maxStack > 1) {
                auto minimum = source.number(item->base.sourceRow, "minstack");
                auto maximum = source.number(item->base.sourceRow, quiver ? "maxstack" : "spawnstack");
                if (!minimum || *minimum < 0 || unsigned(*minimum) > item->maxStack) {
                    plan.deferred = "Unverified stack bounds: " + item->code;
                    return false;
                }
                if (!quiver && (!maximum || *maximum < *minimum || !*maximum))
                    maximum = int(item->maxStack);
                if (!maximum || *maximum < *minimum || unsigned(*maximum) > item->maxStack) {
                    plan.deferred = "Unverified stack bounds: " + item->code;
                    return false;
                }
                quantity = std::max(1u, unsigned(*minimum) + below(unsigned(*maximum - *minimum)));
            }
            plan.drops.push_back({item->code, quantity, {2, 3}, unsigned(itemLevel)});
            return plan.drops.size() < 6;
        });
    plan.randomState = roll.randomState;
    plan.noDrops = roll.noDrops;
    if (!plan.deferred.empty()) {
        plan.drops.clear();
        return plan;
    }
    return plan;
}
} // namespace d2x