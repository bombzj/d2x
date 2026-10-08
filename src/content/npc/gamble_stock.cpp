#include "vendor_stock.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_pricing.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace { unsigned below(uint64_t &seed,unsigned bound) { return limitedRandom(seed,bound); } }
bool npcCanGamble(std::string_view npc) { return npc=="gheed" || npc=="elzix" || npc=="alkor" || npc=="jamella" || npc=="nihlathak" || npc=="drehya"; }
std::vector<VendorOffer> planGambleStock(const ClassicData &data, unsigned playerLevel,
    int difficulty, uint64_t &seed, const std::set<size_t> &usedUniques,
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
        unsigned failedDurability=1;
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
            } else {quality = unique ? ItemQuality::Rare : ItemQuality::Magic;failedDurability=unique?3u:2u;}
        }
        if (quality == ItemQuality::Magic || quality == ItemQuality::Rare) {
            auto result = rollAffixItem(data, *item, quality, level, seed, characterClass);
            seed = result.randomState;
            if (!result.deferred.empty()) throw std::runtime_error(result.deferred);
            generated = std::move(result.generation);
        }
        generated.durabilityMultiplier=failedDurability;
        if (!item->artAvailable || !base->artAvailable)
            throw std::runtime_error("Missing original gambling item art: " + item->code);
        int defense = 0;
        if (item->family == ItemFamily::Armor)
            defense = item->base.minDefense.value() +
                int(below(seed, unsigned(item->base.maxDefense.value() - item->base.minDefense.value() + 1)));
        unsigned quantity = item->equipment.throwable && item->equipment.repairable ? item->maxStack : 1;
        VendorOffer offer{index + 1, item->code, quantity, unsigned(level), itemGamblePrice(data,base->code,int(playerLevel),101,0).value(),
                          defense, 0, false, {}, {}};
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
