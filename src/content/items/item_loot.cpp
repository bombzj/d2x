#include "core/random.hpp"
#include "item_quality.hpp"
#include "item_magic_loot.hpp"
#include "item_grades.hpp"
#include "item_properties.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace d2x {
namespace {
bool appendSelectedItem(const ClassicData &data,const DataTable &ratios,const TreasureSelection &selection,
    int itemLevel,std::string_view characterClass,int magicFind,int goldFind,std::optional<DropQuality> forcedQuality,
    bool superiorFloor,uint64_t &sourceRandom,LootPlan &plan,std::set<size_t> &selectedUniques,bool hellBovine,
    const ItemSeedSource &seedSource,bool nativeCreation=false,int preferredPrefix=0,int preferredSuffix=0) {
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
    const SpecialItemRecord *preferred=nullptr;
    if(!data.items.find(code)) {
        for(const auto &record:data.uniqueItems) if(record.name==code) {preferred=&record;forcedQuality=DropQuality::Unique;break;}
        if(!preferred) for(const auto &record:data.setItems) if(record.name==code) {preferred=&record;forcedQuality=DropQuality::Set;break;}
        if(preferred) code=preferred->code;
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
    std::optional<ItemSeeds> creationSeeds;
    std::optional<uint64_t> creationRandom;
    DropQuality requested;
    if((superiorFloor || nativeCreation) && !forcedQuality) {
        creationSeeds=seedSource?seedSource():ItemSeeds{rollRandom(sourceRandom),rollRandom(sourceRandom)};
        const auto quality=rollCreatedItemQuality(rules,itemLevel,initialRandom(creationSeeds->properties));
        creationRandom=quality.randomState;requested=quality.quality;
    } else if (forcedQuality) {
        // ItemMode::sub_6FC4C5F0: type restrictions still apply to forced qualities.
        requested = rules.normalOnly ? DropQuality::Normal
            : rules.uniqueOnly ? DropQuality::Unique : *forcedQuality;
    } else {
        auto quality = rollItemQuality(rules, itemLevel, std::clamp(magicFind, -1000000, 1000000),
                                       selection.quality, sourceRandom);
        sourceRandom = quality.randomState;
        requested = quality.quality;
    }
    if(superiorFloor && requested==DropQuality::Inferior) requested=DropQuality::Superior;
    if(rules.magicOnly && (requested==DropQuality::Inferior || requested==DropQuality::Normal || requested==DropQuality::Superior)) requested=DropQuality::Magic;
    if(rules.uniqueOnly) requested=DropQuality::Unique;
    if(rules.normalOnly) requested=DropQuality::Normal;
    ItemSeeds seeds=creationSeeds?*creationSeeds:seedSource?seedSource():ItemSeeds{rollRandom(sourceRandom),rollRandom(sourceRandom)};
    auto random=creationRandom.value_or(initialRandom(seeds.properties));
    auto unitRandom=initialRandom(seeds.unit);
    ItemGeneration generation;
    bool unforcedCreation=creationRandom.has_value();
    const auto restartQuality=[&](uint64_t restart) {
        random=restart;seeds.properties=uint32_t(restart);
        if(unforcedCreation) random=rollCreatedItemQuality(rules,itemLevel,random).randomState;
        unforcedCreation=false;
    };
    while (requested != DropQuality::Normal) {
        const auto restart=initialRandom(uint32_t(random));
        if (requested == DropQuality::Unique || requested == DropQuality::Set) {
            const bool unique = requested == DropQuality::Unique;
            const char *table = unique ? "uniqueitems" : "setitems";
            if (!data.tables.contains(table)) {
                plan.deferred = std::string("Missing original ") + table + " table";
                return true;
            }
            const auto &records = unique ? data.uniqueItems : data.setItems;
            auto choice = rollSpecialItem(records, item->code, itemLevel, random,
                                          unique ? selectedUniques : std::set<size_t>{}, hellBovine,preferred?std::optional<size_t>{preferred->row}:std::nullopt);
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
                auto properties = rollItemProperties(data,found->properties, random,itemLevel);
                random = properties.randomState;
                generation.quality = unique ? ItemQuality::Unique : ItemQuality::Set;
                generation.specialRow = int32_t(found->row);
                if (unique && !found->noLimit)
                    selectedUniques.insert(found->row);
                generation.requiredLevel = found->requiredLevel;
                generation.propertyRolls = std::move(properties.values);
                generation.operationRolls=std::move(properties.operations);
                break;
            }
            generation.durabilityMultiplier=unique?3u:2u;
            restartQuality(restart);
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
                                           itemLevel, random, characterClass,preferredPrefix,preferredSuffix);
            random = generated.randomState;
            if (generated.deferred.empty()) {
                const auto multiplier=generation.durabilityMultiplier;
                generation = std::move(generated.generation); generation.durabilityMultiplier=multiplier;
                break;
            }
            requested = requested == DropQuality::Rare ? DropQuality::Magic
                                                       : DropQuality::Superior;
            restartQuality(restart);
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
                const auto multiplier=generation.durabilityMultiplier;
                generation = std::move(generated.generation);
                generation.durabilityMultiplier=multiplier;
                break;
            }
            restartQuality(restart);
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
        ((item->family == ItemFamily::Misc && !rules.normalOnly && !(data.pd2Host && quiver)) ||
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
        return limitedRandom(unitRandom,bound);
    };
    unsigned quantity = 1;
    if (gold) {
        quantity = unsigned(itemLevel) + below(5 * unsigned(itemLevel));
        quantity = unsigned(uint64_t(quantity) * goldMultiplier / 256);
        quantity = unsigned(std::clamp<int64_t>(int64_t(quantity)*(100+int64_t(goldFind))/100,1,item->maxStack));
        if (!quantity || quantity > item->maxStack) {
            plan.deferred = "Gold pile exceeds supported original stack limit";
            return true;
        }
    }
    else if (item->maxStack > 1) {
        auto minimum = source.number(item->base.sourceRow, "minstack");
        auto maximum = source.number(item->base.sourceRow, quiver || item->family==ItemFamily::Weapon ? "maxstack" : "spawnstack");
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
    generation.rollNaturalSockets = true;
    generation.rollNaturalEthereal = true;
    generation.seeds=seeds;
    generation.propertyRandom=random;
    generation.unitRandom=unitRandom;
    plan.drops.push_back({item->code, quantity, {2, 3}, unsigned(itemLevel),
                          std::move(generation)});
    return plan.drops.size() < 6;
}
}
LootPlan planItemLoot(const ClassicData &data, const DataTable &ratios, std::string_view root,
                      int itemLevel, int upgradeLevel, uint64_t seed,
                      const std::set<size_t> &usedUniques, std::string_view characterClass,
                      int magicFind, int goldFind, std::optional<DropQuality> forcedQuality, unsigned effectivePlayers, bool ignoreNoDrop,
                      bool hellBovine,const ItemSeedSource &seeds) {
    if ((data.profile != "lod-named-txt-v1" && !data.pd2Host) || itemLevel < 1 || itemLevel > (data.pd2Host?127:99) ||
        upgradeLevel < 0 || upgradeLevel > (data.pd2Host?127:99))
        throw std::runtime_error("Unsupported item loot profile or level");
    LootPlan plan;
    plan.randomState = seed;
    std::set<size_t> selectedUniques = usedUniques;
    auto roll = selectTreasure(data.treasures, root, seed, upgradeLevel,
        [&](const TreasureSelection &selection, uint64_t &random) {
            return appendSelectedItem(data,ratios,selection,itemLevel,characterClass,magicFind,goldFind,forcedQuality,false,random,plan,selectedUniques,hellBovine,seeds);
        }, effectivePlayers, ignoreNoDrop);
    plan.randomState = roll.randomState;
    plan.noDrops = roll.noDrops;
    return plan;
}
LootPlan planSelectedItem(const ClassicData &data,std::string_view code,int level,uint64_t seed,
    const std::set<size_t> &used,std::string_view characterClass,std::optional<DropQuality> forcedQuality,bool superiorFloor,const ItemSeedSource &seeds,
    int preferredPrefix,int preferredSuffix) {
    if(level<1 || level>(data.pd2Host?131:127)) throw std::runtime_error("Selected item level is outside native bounds");
    LootPlan plan;auto random=seed;auto uniques=used;
    appendSelectedItem(data,data.tables.at("itemratio"),TreasureSelection{std::string(code),{},{}},level,characterClass,0,0,forcedQuality,superiorFloor,random,plan,uniques,false,seeds,false,preferredPrefix,preferredSuffix);
    plan.randomState=random;return plan;
}
LootPlan planCreatedItem(const ClassicData &data,std::string_view code,int level,
    const std::set<size_t> &used,const ItemSeeds &seeds) {
    if(level<1 || level>127) throw std::runtime_error("Created item level is outside native bounds");
    LootPlan plan;uint64_t sourceRandom{};auto uniques=used;
    appendSelectedItem(data,data.tables.at("itemratio"),TreasureSelection{std::string(code),{},{}},level,{},0,0,{},false,
        sourceRandom,plan,uniques,false,[&] {return seeds;},true);
    plan.randomState=sourceRandom;
    return plan;
}
} // namespace d2x
