#include "core/random.hpp"
#include "item_quality.hpp"
#include "item_magic_loot.hpp"
#include "item_grades.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace d2x {
LootPlan planItemLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                      int itemLevel, int upgradeLevel, uint64_t seed,
                      const std::set<size_t> &usedUniques, std::string_view characterClass,
                      int magicFind, int goldFind, std::optional<DropQuality> forcedQuality) {
    if (data.profile != "lod-named-txt-v1" || itemLevel < 1 || itemLevel > 99 ||
        upgradeLevel < 0 || upgradeLevel > 99)
        throw std::runtime_error("Unsupported item loot profile or level");
    LootPlan plan;
    plan.randomState = seed;
    std::set<size_t> selectedUniques = usedUniques;
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
                    return true;
                }
                if (!goldMultiplier)
                    goldMultiplier = 256;
                code = "gld";
            }
            const auto *item = data.items.find(code);
            if (!item) {
                plan.deferred = "Unresolved TC token: " + selection.code;
                return true;
            }
            const auto &source = data.tables.at(item->base.sourceTable);
            if (source.number(item->base.sourceRow, "quest").value_or(0)) {
                plan.deferred = "Quest item drop requires verified rules: " + item->code;
                return true;
            }
            auto rules = loadItemQualityRules(data, ratios, item->code);
            DropQuality requested;
            if (forcedQuality) {
                // ItemMode::sub_6FC4C5F0: type restrictions still apply to forced qualities.
                requested = rules.normalOnly ? DropQuality::Normal
                    : rules.uniqueOnly ? DropQuality::Unique : *forcedQuality;
            } else {
                auto quality = rollItemQuality(rules, itemLevel, std::clamp(magicFind, 0, 1000000),
                                               selection.quality, random);
                random = quality.randomState;
                requested = quality.quality;
            }
            ItemGeneration generation;
            while (requested != DropQuality::Normal) {
                if (requested == DropQuality::Unique || requested == DropQuality::Set) {
                    const bool unique = requested == DropQuality::Unique;
                    const char *table = unique ? "uniqueitems" : "setitems";
                    if (!data.tables.contains(table)) {
                        plan.deferred = std::string("Missing original ") + table + " table";
                        return true;
                    }
                    const auto &records = unique ? data.uniqueItems : data.setItems;
                    auto choice = rollSpecialItem(records, item->code, itemLevel, random,
                                                  unique ? selectedUniques : std::set<size_t>{});
                    random = choice.randomState;
                    if (choice.row && !choice.alreadyDropped) {
                        auto found = std::find_if(records.begin(), records.end(),
                                                  [&](const auto &record) { return record.row == *choice.row; });
                        if (found == records.end())
                            throw std::logic_error("Selected special item has no source record");
                        if (!found->artAvailable) {
                            plan.deferred = "Original special item art missing: " + found->name;
                            return true;
                        }
                        auto properties = rollSpecialProperties(*found, random);
                        random = properties.randomState;
                        generation.quality = unique ? ItemQuality::Unique : ItemQuality::Set;
                        generation.specialRow = int32_t(found->row);
                        if (unique && !found->noLimit)
                            selectedUniques.insert(found->row);
                        generation.requiredLevel = found->requiredLevel;
                        generation.propertyRolls = std::move(properties.values);
                        break;
                    }
                    requested = unique ? DropQuality::Rare : DropQuality::Magic;
                    continue;
                }
                if (requested == DropQuality::Magic || requested == DropQuality::Rare) {
                    if (requested == DropQuality::Rare && !rules.rareAllowed) {
                        requested = DropQuality::Magic;
                        continue;
                    }
                    if (!data.tables.contains("magicprefix") || !data.tables.contains("magicsuffix")) {
                        plan.deferred = "Missing original magic affix tables";
                        return true;
                    }
                    if (requested == DropQuality::Rare &&
                        (!data.tables.contains("rareprefix") || !data.tables.contains("raresuffix"))) {
                        plan.deferred = "Missing original rare name tables";
                        return true;
                    }
                    auto generated = rollAffixItem(data, *item,
                                                   requested == DropQuality::Magic ? ItemQuality::Magic
                                                                                   : ItemQuality::Rare,
                                                   itemLevel, random, characterClass);
                    random = generated.randomState;
                    if (generated.deferred.empty()) {
                        generation = std::move(generated.generation);
                        break;
                    }
                    requested = requested == DropQuality::Rare ? DropQuality::Magic
                                                               : DropQuality::Superior;
                    continue;
                }
                if (requested == DropQuality::Superior || requested == DropQuality::Inferior) {
                    const bool superior = requested == DropQuality::Superior;
                    if (!data.tables.contains(superior ? "qualityitems" : "lowqualityitems")) {
                        plan.deferred = superior ? "Missing original QualityItems table"
                                                 : "Missing original LowQualityItems table";
                        return true;
                    }
                    auto generated = rollItemGrade(data, *item,
                                                   superior ? ItemQuality::Superior : ItemQuality::Inferior,
                                                   random);
                    random = generated.randomState;
                    if (generated.deferred.empty()) {
                        generation = std::move(generated.generation);
                        break;
                    }
                    requested = DropQuality::Normal;
                    continue;
                }
                plan.deferred = "Unknown item quality request";
                return true;
            }
            const bool gold = item->equipment.isType("gold");
            const bool quiver = !item->equipment.quiver.empty();
            if (generation.quality != ItemQuality::Unique && generation.quality != ItemQuality::Set &&
                !item->artAvailable) {
                plan.deferred = "Original item art missing: " + item->code;
                return true;
            }
            if (generation.quality == ItemQuality::Normal &&
                ((item->family == ItemFamily::Misc && !rules.normalOnly) ||
                 (item->family != ItemFamily::Misc && !item->equipment.known))) {
                plan.deferred = "Unsupported instance: " + item->code + " requested=" +
                                dropQualityName(requested);
                return true;
            }
            if (item->family == ItemFamily::Armor &&
                (!item->base.minDefense || !item->base.maxDefense || *item->base.minDefense < 0 ||
                 *item->base.maxDefense < *item->base.minDefense || *item->base.maxDefense > 1000000)) {
                plan.deferred = "Unverified armor defense: " + item->code;
                return true;
            }
            auto below = [&](unsigned bound) {
                if (!bound)
                    return 0u;
                rollRandom(random);
                return uint32_t(random) % bound;
            };
            unsigned quantity = 1;
            if (gold) {
                quantity = unsigned(itemLevel) + below(5 * unsigned(itemLevel));
                quantity = unsigned(uint64_t(quantity) * goldMultiplier / 256);
                quantity = unsigned(std::min<uint64_t>(item->maxStack,
                    uint64_t(quantity) * uint64_t(100 + std::max(0, goldFind)) / 100));
                if (!quantity || quantity > item->maxStack) {
                    plan.deferred = "Gold pile exceeds supported original stack limit";
                    return true;
                }
            }
            else if (item->maxStack > 1) {
                auto minimum = source.number(item->base.sourceRow, "minstack");
                auto maximum = source.number(item->base.sourceRow, quiver ? "maxstack" : "spawnstack");
                if (!minimum || *minimum < 0 || unsigned(*minimum) > item->maxStack) {
                    plan.deferred = "Unverified stack bounds: " + item->code;
                    return true;
                }
                if (!quiver && (!maximum || *maximum < *minimum || !*maximum))
                    maximum = int(item->maxStack);
                if (!maximum || *maximum < *minimum || unsigned(*maximum) > item->maxStack) {
                    plan.deferred = "Unverified stack bounds: " + item->code;
                    return true;
                }
                quantity = std::max(1u, unsigned(*minimum) + below(unsigned(*maximum - *minimum)));
            }
            plan.drops.push_back({item->code, quantity, {2, 3}, unsigned(itemLevel),
                                  std::move(generation)});
            return plan.drops.size() < 6;
        });
    plan.randomState = roll.randomState;
    plan.noDrops = roll.noDrops;
    return plan;
}
} // namespace d2x
