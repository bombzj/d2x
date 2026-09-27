#include "core/random.hpp"
#include "gameplay/npc/store.hpp"
#include "item_magic_loot.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
unsigned below(uint64_t &seed, unsigned bound) {
    return limitedRandom(seed, bound);
}
unsigned price(const ClassicData &data, const ItemDefinition &base, int level) {
    const auto &table = data.tables.at(base.base.sourceTable);
    const auto row = base.base.sourceRow;
    if (base.equipment.isType("ring") || base.equipment.isType("amul"))
        return unsigned(std::max(1, table.number(row, "gamble cost").value_or(0)));
    // D2Common ITEMS_GetTransactionCost: expansion gambling uses the normal
    // base, player level and the cost of both possible upgraded bases.
    int64_t exceptionalWeight = 0, eliteWeight = 0, exceptionalCost = 0, eliteCost = 0;
    auto upgrade = [&](const char *column, int divisor, int64_t &weight, int64_t &cost) {
        if (const auto *item = data.items.find(table.value(row, column))) {
            weight = std::max(0, 100 * (level - item->base.level.value_or(0)) / divisor + 1);
            cost = item->base.cost.value_or(0);
        }
    };
    upgrade("ubercode", 2, exceptionalWeight, exceptionalCost);
    upgrade("ultracode", 4, eliteWeight, eliteCost);
    const int stack = std::max(1, (table.number(row, "minstack").value_or(0) +
                                   table.number(row, "maxstack").value_or(0)) / 2);
    const int64_t cost = exceptionalWeight * exceptionalCost + eliteWeight * eliteCost +
        int64_t(stack) * base.base.cost.value_or(0) * (10000 - exceptionalWeight - eliteWeight);
    level = std::max(5, level);
    const int qlvl = base.base.level.value_or(0);
    const int64_t value = ((2 * level + 1) / 3 + 20) *
        (cost / 10000 + 250 * (level + std::max(qlvl - 45, 0) - qlvl / 2) / 3) / 15;
    return unsigned(std::clamp<int64_t>(value, 1, 2000000000));
}
}
bool npcCanGamble(std::string_view npc) {
    // Engine services, not localized names; D2MOO SUNITPROXY_InitializeNpcControl.
    return npc == "gheed" || npc == "elzix" || npc == "alkor" || npc == "jamella" ||
           npc == "nihlathak" || npc == "drehya";
}
ItemInstance vendorItem(const VendorOffer &offer, const ClassicData &data, bool hidden) {
    ItemInstance item;
    item.definition = hidden && !offer.displayCode.empty() ? offer.displayCode : offer.code;
    const auto &definition = *data.items.find(item.definition);
    item.quantity = offer.quantity;
    item.level = offer.level;
    item.defense = offer.defense;
    item.durability = definition.maxDurability;
    item.charges = definition.bookInitialCharges;
    if (!hidden) {
        const auto &g = offer.generation;
        item.quality = g.quality;
        item.specialRow = g.specialRow;
        item.requiredLevel = g.requiredLevel;
        item.gradeRow = g.gradeRow;
        item.rarePrefixRow = g.rarePrefixRow;
        item.rareSuffixRow = g.rareSuffixRow;
        item.propertyRolls = g.propertyRolls;
        item.affixes = g.affixes;
        int durabilityPercent = 0, durabilityAdd = 0;
        for (const auto &stat : resolveItemStats(data, item, 1)) {
            if (stat.effect == "item_maxdurability_percent") durabilityPercent += stat.value;
            if (stat.effect == "maxdurability") durabilityAdd += stat.value;
        }
        if (item.durability) {
            if (item.quality == ItemQuality::Inferior) item.durability = std::max(1u, item.durability / 3);
            item.durability = unsigned(std::clamp(int(item.durability) * (100 + durabilityPercent) / 100 + durabilityAdd, 1, 255));
        }
    }
    return item;
}
std::vector<VendorOffer> planGambleStock(const ClassicData &data, unsigned playerLevel,
    int difficulty, uint64_t &seed, const std::set<uint32_t> &usedUniques,
    std::string_view characterClass) {
    if (difficulty < 0 || difficulty > 2 || !playerLevel || playerLevel > 99)
        throw std::runtime_error("Invalid gamble parameters");
    const auto &pool = data.tables.at("gamble");
    const auto &chances = data.tables.at("difficultylevels");
    const auto chance = [&](const char *field) {
        auto value = chances.number(size_t(difficulty), field);
        if (!value || *value < 0) throw std::runtime_error("Missing original gambling chance");
        return unsigned(*value);
    };
    const unsigned uniqueChance = chance("GambleUnique"), setChance = chance("GambleSet"),
                   rareChance = chance("GambleRare");
    std::set<size_t> uniques(usedUniques.begin(), usedUniques.end());
    std::vector<VendorOffer> offers;
    for (unsigned index = 0; index < 14; ++index) {
        const int level = std::clamp(int(playerLevel) + int(below(seed, 10)) - 5, 5, 99);
        std::vector<const ItemDefinition *> eligible;
        for (size_t row = 0; row < pool.rows().size(); ++row)
            if (const auto *item = data.items.find(pool.value(row, "code"));
                item && item->base.level.value_or(100) <= level)
                eligible.push_back(item);
        std::stable_sort(eligible.begin(), eligible.end(), [](auto a, auto b) {
            return a->base.level < b->base.level;
        });
        if (eligible.empty()) throw std::runtime_error("Empty original gamble pool");
        const auto *base = eligible[below(seed, unsigned(eligible.size()))];
        // The first two stock slots are always the ring and amulet from Gamble.txt.
        if (index < 2)
            for (const auto *candidate : eligible)
                if (candidate->equipment.isType(index == 0 ? "ring" : "amul")) {
                    base = candidate;
                    break;
                }
        const auto *item = base;
        const auto &source = data.tables.at(base->base.sourceTable);
        auto upgrade = [&](const char *column, unsigned multiplier) {
            const auto *next = data.items.find(source.value(base->base.sourceRow, column));
            if (!next) return false;
            const int64_t odds = int64_t(multiplier) * (level - next->base.level.value_or(100)) + 1;
            if (odds > 0 && below(seed, 10000) < uint64_t(odds)) { item = next; return true; }
            return false;
        };
        if (!upgrade("ubercode", chance("GambleUber")))
            upgrade("ultracode", chance("GambleUltra"));
        const unsigned roll = below(seed, 100000);
        ItemQuality quality = roll < uniqueChance ? ItemQuality::Unique :
            roll < uniqueChance + setChance ? ItemQuality::Set :
            roll < uniqueChance + setChance + rareChance ? ItemQuality::Rare : ItemQuality::Magic;
        ItemGeneration generated;
        if (quality == ItemQuality::Unique || quality == ItemQuality::Set) {
            bool unique = quality == ItemQuality::Unique;
            const auto &records = unique ? data.uniqueItems : data.setItems;
            auto selected = rollSpecialItem(records, item->code, level, seed, unique ? uniques : std::set<size_t>{});
            seed = selected.randomState;
            if (selected.row && !selected.alreadyDropped) {
                const auto found = std::find_if(records.begin(), records.end(), [&](const auto &r) { return r.row == *selected.row; });
                if (!found->artAvailable) throw std::runtime_error("Missing original gambling item art: " + found->name);
                auto properties = rollSpecialProperties(*found, seed);
                seed = properties.randomState;
                generated.quality = quality;
                generated.specialRow = int32_t(found->row);
                generated.requiredLevel = found->requiredLevel;
                generated.propertyRolls = std::move(properties.values);
                if (unique && !found->noLimit) uniques.insert(found->row);
            } else quality = unique ? ItemQuality::Rare : ItemQuality::Magic;
        }
        if (quality == ItemQuality::Magic || quality == ItemQuality::Rare) {
            auto result = rollAffixItem(data, *item, quality, level, seed, characterClass);
            seed = result.randomState;
            if (!result.deferred.empty()) throw std::runtime_error(result.deferred);
            generated = std::move(result.generation);
        }
        if (!item->artAvailable || !base->artAvailable)
            throw std::runtime_error("Missing original gambling item art: " + item->code);
        int defense = 0;
        if (item->family == ItemFamily::Armor)
            defense = item->base.minDefense.value() +
                int(below(seed, unsigned(item->base.maxDefense.value() - item->base.minDefense.value() + 1)));
        unsigned quantity = item->equipment.throwable && item->equipment.repairable ? item->maxStack : 1;
        VendorOffer offer{index + 1, item->code, quantity, unsigned(level), price(data, *base, int(playerLevel)),
                          defense, 0, false};
        offer.generation = std::move(generated);
        offer.displayCode = base->code;
        for (const auto &stat : resolveItemStats(data, vendorItem(offer, data), 1)) {
            if (stat.effect == "item_armor_percent" && stat.value && item->base.maxDefense)
                offer.defense = *item->base.maxDefense + 1;
            if (stat.effect == "item_extra_stack")
                offer.quantity = unsigned(std::clamp(int(item->maxStack) + stat.value, 1, 511));
        }
        offers.push_back(std::move(offer));
    }
    return offers;
}
} // namespace d2x
