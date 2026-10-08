#include "core/random.hpp"
#include "vendor_stock.hpp"
#include "content/items/item_grades.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_pricing.hpp"
#include "content/items/item_properties.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace d2x {
namespace {
unsigned below(uint64_t &random, unsigned bound) {
    return limitedRandom(random, bound);
}
}
bool npcCanRepair(std::string_view npc) {
    return npc == "charsi" || npc == "fara" || npc == "hratli" || npc == "halbu" || npc == "larzuk";
}
bool npcCanIdentify(std::string_view npc) {
    return npc == "cain2" || npc == "cain3" || npc == "cain4" || npc == "cain5" || npc == "cain6";
}
bool npcCanHeal(std::string_view npc) {
    return npc == "akara" || npc == "atma" || npc == "fara" || npc == "ormus" || npc == "jamella" || npc == "malah";
}
std::vector<VendorOffer> planVendorStock(const ClassicData &data, const VendorDefinition &vendor,
                                         unsigned playerLevel, int difficulty, uint64_t seed) {
    if (!playerLevel || playerLevel > 99 || difficulty < 0 || difficulty > 2 ||
        vendor.sellMultiplier <= 0 || vendor.act >= 5)
        throw std::runtime_error("Unsupported vendor stock parameters");
    constexpr std::array<unsigned, 5> caps{12, 20, 28, 36, 45};
    const unsigned level = difficulty == 0 ? std::min(playerLevel + 5, caps[vendor.act])
                                           : std::min(playerLevel + 5, 99u);
    std::vector<VendorOffer> offers;
    unsigned failures = 0;
    auto generate = [&](const VendorItemRule &rule, ItemQuality quality) {
        const auto *item = data.items.find(rule.code);
        if (!item) { ++failures; return; }
        const auto &source = data.tables.at(item->base.sourceTable);
        const size_t sourceRow = item->base.sourceRow;
        auto upgrade = [&](const char *column) {
            if (const auto *next = data.items.find(source.value(sourceRow, column))) {
                item = next;
                return true;
            }
            return false;
        };
        // SUnitNpc: difficulty upgrades apply to the selected source record.
        if (difficulty && playerLevel > 25) {
            const auto roll = below(seed, 100000);
            if (difficulty == 1) {
                if (!(roll < 64 * level + 4000 && upgrade("ubercode"))) upgrade("NightmareUpgrade");
            } else {
                if (!(roll < 16 * level + 1000 && upgrade("ultracode")))
                    if (roll < 128 * level + 5000) upgrade("ubercode");
                upgrade("HellUpgrade");
            }
        }
        if (!item->artAvailable) { ++failures; return; }
        ItemGeneration generation;
        if (quality == ItemQuality::Magic) {
            auto rolled = rollAffixItem(data, *item, quality, int(level), seed, {});
            seed = rolled.randomState;
            if (!rolled.deferred.empty()) { ++failures; return; }
            generation = std::move(rolled.generation);
        } else if (quality != ItemQuality::Normal && item->family != ItemFamily::Misc) {
            auto rolled = rollItemGrade(data, *item, quality, seed);
            seed = rolled.randomState;
            if (!rolled.deferred.empty()) { ++failures; return; }
            generation = std::move(rolled.generation);
        }
        unsigned quantity = (item->equipment.throwable && item->equipment.repairable) ||
                            (rule.permanent && !item->equipment.quiver.empty()) ? item->maxStack : 1;
        int defense = 0;
        if (item->family == ItemFamily::Armor) {
            auto minimum = item->base.minDefense, maximum = item->base.maxDefense;
            if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum) { ++failures; return; }
            defense = *minimum + int(below(seed, unsigned(*maximum - *minimum + 1)));
            if (generation.quality == ItemQuality::Inferior) defense = std::max(1, defense * 75 / 100);
        }
        VendorOffer offer{uint32_t(offers.size() + 1), item->code, quantity, level, 0,
                          defense, rule.storePage, rule.permanent, {}, {}};
        offer.generation = std::move(generation);
        auto instance = vendorItem(offer, data);
        // Enhanced defense uses maxac+1 as the underlying armor roll.
        for (const auto &stat : resolveItemStats(data, instance, 1)) {
            if (stat.effect == "item_armor_percent" && stat.value && item->base.maxDefense)
                instance.defense = offer.defense = *item->base.maxDefense + 1;
            if (stat.effect == "item_extra_stack")
                instance.quantity = offer.quantity = unsigned(std::clamp(int(item->maxStack) + stat.value, 1, 511));
        }
        auto quote = itemTradePrice(data, instance, vendor);
        if (!quote) { ++failures; return; }
        offer.price = *quote;
        offers.push_back(std::move(offer));
    };
    // MPQ NPC columns decide each vendor's distinct pool. Normal and magic
    // counts are independent; 32 is a failure limit, not a stock-size limit.
    for (const auto &rule : vendor.items) {
        if (rule.permanent || rule.level > int(level)) continue;
        unsigned count = level < 25 ? unsigned(rule.minimum) +
            below(seed, unsigned(rule.maximum - rule.minimum + 1)) : 0;
        for (unsigned index = 0; index < count; ++index) {
            const unsigned roll = below(seed, 100);
            auto quality = level >= 10 && roll >= 75 ? ItemQuality::Superior :
                level >= 5 && level < 10 && roll > 85 ? ItemQuality::Superior :
                level < 5 && roll > 90 ? ItemQuality::Inferior : ItemQuality::Normal;
            generate(rule, quality);
            if (failures > 32) return offers;
        }
        if (rule.magicEligible && rule.magicLevel <= int(level)) {
            const unsigned extra = level < 25 ? 1 : 2 + below(seed, 2);
            count = unsigned(rule.magicMinimum) + below(seed,
                extra + unsigned(rule.magicMaximum - rule.magicMinimum));
            for (unsigned index = 0; index < count; ++index) generate(rule, ItemQuality::Magic);
        }
    }
    for (const auto &rule : vendor.items)
        if (rule.permanent) {
            generate(rule, ItemQuality::Normal);
            if (failures > 32) break;
        }
    return offers;
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
} // namespace d2x
