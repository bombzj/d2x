#include "item_content.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/item_quality.hpp"
#include "gameplay/items/quality.hpp"
#include <array>
#include "content/items/cube_data.hpp"
#include "gameplay/items/staffmods.hpp"
#include "gameplay/loot/special.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
DropQuality nativeDropQuality(ItemQuality quality) {
    constexpr std::array qualities{DropQuality::Normal,DropQuality::Magic,DropQuality::Rare,DropQuality::Set,
        DropQuality::Unique,DropQuality::Superior,DropQuality::Inferior};
    if(size_t(quality)>=qualities.size()) throw std::runtime_error("Unsupported native item quality");
    return qualities.at(size_t(quality));
}
ItemInstance prepareNativeItem(const ClassicData &data,std::string_view code,int level,int difficulty,
    uint64_t &gameRandom,std::optional<DropQuality> quality,const std::set<size_t> &uniques,NativeItemUse use) {
    const auto seeds=rollNativeItemSeeds(gameRandom);
    auto plan=quality?planSelectedItem(data,code,level,0,uniques,{},*quality,false,[&]{return seeds;}):
        planCreatedItem(data,code,level,uniques,seeds);
    if(!plan.deferred.empty() || plan.drops.size()!=1)
        throw std::runtime_error(plan.deferred.empty()?"Native item construction failed":plan.deferred);
    auto &drop=plan.drops.front();drop.generation.rollNaturalEthereal=false;
    drop.generation.suppressNaturalSockets=use==NativeItemUse::Equipment;
    uint64_t unused{};auto item=prepareItem(data,drop,unused,difficulty);
    if(use==NativeItemUse::Equipment) {
        item.nativeFlags|=0x1000;
        auto identified=item;identified.identified=true;
        item.durability=itemMaximumDurability(data,identified,resolveItemStats(data,identified,level));
    }
    return item;
}
namespace {
void appendStats(const ClassicData &data, ItemInstance &item, const std::vector<ResolvedItemStat> &stats) {
    for(const auto &stat:stats) {
        const auto definition=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &v){return v.name==stat.name;});
        if(definition==data.itemStats.end() || !definition->id) throw std::runtime_error("Unresolved generated item stat");
        auto existing=std::find_if(item.savedStats.begin(),item.savedStats.end(),[&](const auto &v){return v.id==*definition->id && v.parameter==stat.layer;});
        if(existing==item.savedStats.end()) item.savedStats.push_back({*definition->id,stat.layer,stat.rawValue});
        else if(stat.replace) existing->value=stat.rawValue;
        else existing->value+=stat.rawValue;
    }
}
void staffProperties(const ClassicData &data,const ItemDefinition &base,ItemInstance &item,uint64_t &random,int staffmodBias) {
    if(item.quality==ItemQuality::Unique || item.quality==ItemQuality::Set) return;
    const auto &types=data.tables.at("itemtypes"),&skills=data.tables.at("skills");
    std::string staffClass;
    for(size_t row=0;row<types.rows().size();++row)
        if(base.base.type==types.value(row,"Code") && !types.value(row,"StaffMods").empty()) {staffClass=types.value(row,"StaffMods");break;}
    if(!staffClass.empty()) {
        int first=-1;
        for(size_t row=0;row<skills.rows().size();++row) if(skills.value(row,"charclass")==staffClass) {first=skills.number(row,"Id").value();break;}
        if(first<0) throw std::runtime_error("Original staffmod skill class is unavailable");
        const auto eligible=[&](int skill) {
            for(size_t row=0;row<skills.rows().size();++row) if(skills.number(row,"Id")==skill) {
                const auto required=skills.value(row,"itypea1"); return required.empty() || base.equipment.isType(required);
            }
            return false;
        };
        for(const auto &[skill,rank]:rollStaffmods(int(item.level),first,item.quality==ItemQuality::Inferior,staffmodBias,eligible,random)) {
            const auto existing=std::find_if(item.savedStats.begin(),item.savedStats.end(),[&](const auto &entry){return entry.id==107 && entry.parameter==skill;});
            if(existing==item.savedStats.end()) item.savedStats.push_back({107,skill,rank});
            else existing->value=rank;
        }
    }
}
void autoProperties(const ClassicData &data,const ItemDefinition &base,ItemInstance &item,uint64_t &random) {
    if(item.quality==ItemQuality::Unique || item.quality==ItemQuality::Set) return;
    const int group=data.tables.at(base.base.sourceTable).number(base.base.sourceRow,"auto prefix").value_or(0);
    if(group) {
        std::vector<MagicAffixRecord> candidates;
        for(const auto &record:data.autoMagic) if(record.group==group) candidates.push_back(record);
        std::vector<int> usedGroups;
        for(const auto &affix:item.affixes) {
            const auto &records=affix.prefix?data.magicPrefixes:data.magicSuffixes;
            for(const auto &record:records) if(int32_t(record.row)==affix.row) usedGroups.push_back(record.group);
        }
        const int magic=base.base.magicLevel.value_or(0);
        const bool rare=item.quality==ItemQuality::Rare || item.quality==ItemQuality::Crafted || item.quality==ItemQuality::Tempered;
        auto roll=rollMagicAffix(candidates,base.equipment.types,base.equipment.requiredClass,
            itemAffixLevel(int(item.level),base.base.level.value_or(0),magic),magic,rare,base.base.sockets.value_or(0)>0,usedGroups,true,random);
        random=roll.randomState;
        if(roll.row) {
            const auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto &v){return v.row==*roll.row;});
            auto values=rollItemProperties(data,found->properties,random,int(item.level));random=values.randomState;
            for(size_t i=0;i<found->properties.size();++i) appendStats(data,item,resolvePropertyStats(data,found->properties[i],values.values[i],int(item.level),int(item.level),values.operations[i]));
            item.nativeAutoAffix=unsigned(found->row+1);item.requiredLevel=std::max(item.requiredLevel,found->requiredLevel);
        }
    }
}
}
ItemInstance prepareItem(const ClassicData &data, const LootDrop &drop, uint64_t &random, int difficulty,int staffmodBias) {
    const auto *definition = data.items.find(drop.code);
    if (!definition || !drop.quantity || drop.quantity > definition->maxStack || drop.level < 1 || drop.level > 127)
        throw std::runtime_error("Invalid prepared item generation");
    if (definition->equipment.isType("body"))
        throw std::runtime_error("A body part requires its original player/monster identity; generic generation cannot create one");
    // Ported from master's InventoryService::createItem. Placement, ownership
    // and entity allocation deliberately remain in the authority.
    const auto &generation = drop.generation;
    ItemInstance item;
    item.definition = drop.code; item.quantity = definition->bookCapacity?1:drop.quantity; item.level = drop.level;
    item.charges = definition->bookCapacity?drop.quantity:0; item.quality = generation.quality;
    item.identified = item.quality != ItemQuality::Magic && item.quality != ItemQuality::Rare &&
        item.quality != ItemQuality::Set && item.quality != ItemQuality::Unique && item.quality != ItemQuality::Crafted;
    item.specialRow = generation.specialRow; item.requiredLevel = generation.requiredLevel;
    item.gradeRow = generation.gradeRow; item.rarePrefixRow = generation.rarePrefixRow;
    item.rareSuffixRow = generation.rareSuffixRow; item.propertyRolls = generation.propertyRolls; item.affixes = generation.affixes;
    item.operationRolls=generation.operationRolls;
    auto identified = item; identified.identified = true;
    const auto stats = resolveItemStats(data, identified, int(item.level));
    const auto property = [&](std::string_view name) { int value = 0; for (const auto &stat : stats) if (stat.name == name) value += int(stat.value); return value; };
    item.durability = itemMaximumDurability(data, identified, stats);
    const auto seeds=generation.seeds?*generation.seeds:ItemSeeds{rollRandom(random),rollRandom(random)};
    item.nativeSeed = seeds.properties;
    auto local = generation.propertyRandom.value_or(initialRandom(seeds.properties));
    auto unitRandom = generation.unitRandom.value_or(initialRandom(seeds.unit));
    const unsigned halfDurability=definition->maxDurability/2;
    unsigned baseDurability=0;
    if(definition->family==ItemFamily::Armor || definition->family==ItemFamily::Weapon)
        baseDurability=std::min(255u,halfDurability+limitedRandom(unitRandom,halfDurability));
    if (definition->family == ItemFamily::Armor) {
        const auto minimum = definition->base.minDefense, maximum = definition->base.maxDefense;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum) throw std::runtime_error("Missing armor defense");
        item.defense = *minimum + int(limitedRandom(unitRandom, unsigned(*maximum - *minimum + 1)));
        if (item.quality == ItemQuality::Inferior) item.defense = inferiorBaseValue(item.defense, 1);
        const auto applyBase=[&](const auto &properties) {
            for(const auto &entry:properties) {
                const auto program=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &record){return record.code==entry.code;});
                if(program==data.properties.end()) throw std::runtime_error("Unknown armor property program");
                for(const auto &operation:program->operations)
                    if((operation.stat=="armorclass" || operation.stat=="item_armor_percent") &&
                        (operation.function==2 || operation.function==4 || (item.quality==ItemQuality::Superior && (operation.function==1 || operation.function==3))))
                        if(*maximum) item.defense=std::max(item.defense+1,*maximum+1);
            }
        };
        if(item.specialRow>=0) for(const auto &record:item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems)
            if(int32_t(record.row)==item.specialRow) applyBase(record.properties);
        for(const auto &affix:item.affixes) for(const auto &record:affix.prefix?data.magicPrefixes:data.magicSuffixes)
            if(int32_t(record.row)==affix.row) applyBase(record.properties);
        if(item.quality==ItemQuality::Superior) for(const auto &record:data.superiorGrades)
            if(int32_t(record.row)==item.gradeRow) applyBase(record.properties);
    }
    if (!definition->inventoryIcons.empty()) {
        item.nativeHasGraphic = true; item.nativeGraphic = limitedRandom(unitRandom, unsigned(definition->inventoryIcons.size()));
    }
    if(item.quality==ItemQuality::Inferior && definition->maxDurability) {
        const auto maximum=std::max(1u,definition->maxDurability/3);
        baseDurability=std::max(1u,maximum/2+limitedRandom(unitRandom,maximum/2));
    }
    const bool visible=item.identified;item.identified=true;
    freezeCubeItem(data,item,&local);
    item.nativeMaxDurability=std::min(255u,item.nativeMaxDurability*generation.durabilityMultiplier);
    staffProperties(data,*definition,item,local,staffmodBias);
    bool forcedEthereal=false;
    const bool baseDurable=definition->maxDurability && !data.tables.at(definition->base.sourceTable).number(definition->base.sourceRow,"nodurability").value_or(0);
    if(item.specialRow>=0) {
        const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
        for(const auto &record:records) if(int(record.row)==item.specialRow) {
            bool indestructible=false;
            for(const auto &property:record.properties) {
                const auto program=std::find_if(data.properties.begin(),data.properties.end(),[&](const auto &entry){return entry.code==property.code;});
                if(program==data.properties.end()) throw std::runtime_error("Unknown special property program");
                for(const auto &operation:program->operations) {
                    if(operation.function==23 && baseDurable && !indestructible) forcedEthereal=true;
                    if(operation.function==20) indestructible=true;
                }
            }
        }
    }
    const bool hasDurability=baseDurable && property("item_indesctructible")<=0;
    const bool canEthereal=hasDurability && !definition->questTag && item.quality!=ItemQuality::Inferior && item.quality!=ItemQuality::Set;
    if(generation.forceEthereal && !canEthereal)
        throw std::runtime_error("This item cannot be ethereal under the original item rules");
    const bool rolledEthereal=canEthereal && generation.rollNaturalEthereal && limitedRandom(local,100)<5;
    const bool generatedEthereal=generation.forceEthereal || rolledEthereal;
    if(forcedEthereal || generatedEthereal) {
        item.nativeFlags|=0x400000u;
        if(definition->family==ItemFamily::Armor) item.defense=item.defense*3/2;
        if(generatedEthereal) item.nativeMaxDurability=item.nativeMaxDurability/2+1;
    }
    const unsigned limit = unsigned(std::max(0, std::min({definition->base.sockets.value_or(0),
        definition->base.socketsByLevel[item.level <= 25 ? 0 : item.level <= 40 ? 1 : 2], definition->width * definition->height, 6})));
    if (definition->maxStack == 1 && limit) {
        if (const auto sockets = property("item_numsockets"); sockets > 0) item.sockets = std::min(limit, unsigned(sockets));
        else if (generation.rollNaturalSockets && (item.quality == ItemQuality::Normal || item.quality == ItemQuality::Superior) &&
            limitedRandom(local, 100) < 33 && !generation.suppressNaturalSockets) {
            constexpr unsigned caps[]{3, 4, 6};
            item.sockets = item.nativeSeed % std::min(limit, caps[std::clamp(difficulty, 0, 2)]) + 1;
        }
    }
    if (item.sockets) item.nativeFlags |= 0x800;
    autoProperties(data,*definition,item,local);
    const auto complete=resolveItemStats(data,item,int(item.level));
    const auto maximumDurability=std::min(255u,itemMaximumDurability(data,item,complete));
    item.durability=generation.rollNaturalEthereal && !generatedEthereal && !property("item_maxdurability_percent")
        ? std::min(maximumDurability,baseDurability*generation.durabilityMultiplier):maximumDurability;
    updateCubeRequiredLevel(data,item);item.identified=visible;
    item.nativeQuestDifficulty = unsigned(difficulty);
    return item;
}
}
