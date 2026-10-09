#include "item_content.hpp"
#include "content/items/item_properties.hpp"
#include "content/items/cube_data.hpp"
#include "gameplay/items/staffmods.hpp"
#include "gameplay/loot/special.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace {
void appendStats(const ClassicData &data, ItemInstance &item, const std::vector<ResolvedItemStat> &stats) {
    for(const auto &stat:stats) {
        const auto definition=std::find_if(data.itemStats.begin(),data.itemStats.end(),[&](const auto &v){return v.name==stat.name;});
        if(definition==data.itemStats.end() || !definition->id) throw std::runtime_error("Unresolved generated item stat");
        auto existing=std::find_if(item.savedStats.begin(),item.savedStats.end(),[&](const auto &v){return v.id==*definition->id && v.parameter==stat.layer;});
        if(existing==item.savedStats.end()) item.savedStats.push_back({*definition->id,stat.layer,stat.rawValue});
        else existing->value+=stat.rawValue;
    }
}
void innateProperties(const ClassicData &data,const ItemDefinition &base,ItemInstance &item,uint64_t &random,int staffmodBias) {
    if(item.quality==ItemQuality::Unique || item.quality==ItemQuality::Set) return;
    const auto &types=data.tables.at("itemtypes"),&skills=data.tables.at("skills");
    std::string staffClass;
    for(size_t row=0;row<types.rows().size();++row)
        if(base.equipment.isType(types.value(row,"Code")) && !types.value(row,"StaffMods").empty()) {staffClass=types.value(row,"StaffMods");break;}
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
        for(const auto &[skill,rank]:rollStaffmods(int(item.level),first,item.quality==ItemQuality::Inferior,staffmodBias,eligible,random)) item.savedStats.push_back({107,skill,rank});
    }
    const int group=data.tables.at(base.base.sourceTable).number(base.base.sourceRow,"auto prefix").value_or(0);
    if(group) {
        std::vector<MagicAffixRecord> candidates;
        for(const auto &record:data.autoMagic) if(record.group==group) candidates.push_back(record);
        const int magic=base.base.magicLevel.value_or(0);
        auto roll=rollMagicAffix(candidates,base.equipment.types,base.equipment.requiredClass,
            itemAffixLevel(int(item.level),base.base.level.value_or(0),magic),magic,false,base.base.sockets.value_or(0)>0,{},true,random);
        random=roll.randomState;
        if(roll.row) {
            const auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto &v){return v.row==*roll.row;});
            auto values=rollDisplayProperties(found->properties,random);random=values.randomState;
            for(size_t i=0;i<found->properties.size();++i) appendStats(data,item,resolvePropertyStats(data,found->properties[i],values.values[i],int(item.level),int(item.level)));
            item.nativeAutoAffix=unsigned(found->row+1);item.requiredLevel=std::max(item.requiredLevel,found->requiredLevel);
        }
    }
}
}
ItemInstance prepareItem(const ClassicData &data, const LootDrop &drop, uint64_t &random, int difficulty,int staffmodBias) {
    const auto *definition = data.items.find(drop.code);
    if (!definition || !drop.quantity || drop.quantity > definition->maxStack || drop.level < 1 || drop.level > 99)
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
    auto identified = item; identified.identified = true;
    const auto stats = resolveItemStats(data, identified, int(item.level));
    const auto property = [&](std::string_view name) { int value = 0; for (const auto &stat : stats) if (stat.name == name) value += int(stat.value); return value; };
    item.durability = itemMaximumDurability(data, identified, stats);
    item.nativeSeed = rollRandom(random);
    auto local = initialRandom(item.nativeSeed);
    if (!definition->inventoryIcons.empty()) {
        item.nativeHasGraphic = true; item.nativeGraphic = limitedRandom(local, unsigned(definition->inventoryIcons.size()));
    }
    if (definition->family == ItemFamily::Armor) {
        const auto minimum = definition->base.minDefense, maximum = definition->base.maxDefense;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum) throw std::runtime_error("Missing armor defense");
        item.defense = *minimum + int(limitedRandom(local, unsigned(*maximum - *minimum + 1)));
        if (item.quality == ItemQuality::Inferior) item.defense = std::max(1, item.defense * 75 / 100);
        if (property("item_armor_percent")) item.defense = *maximum + 1;
    }
    const unsigned limit = unsigned(std::max(0, std::min({definition->base.sockets.value_or(0),
        definition->base.socketsByLevel[item.level <= 25 ? 0 : item.level <= 40 ? 1 : 2], definition->width * definition->height, 6})));
    if (definition->maxStack == 1 && limit) {
        if (const auto sockets = property("item_numsockets"); sockets > 0) item.sockets = std::min(limit, unsigned(sockets));
        else if (generation.rollNaturalSockets && (item.quality == ItemQuality::Normal || item.quality == ItemQuality::Superior) && limitedRandom(local, 100) < 33) {
            constexpr unsigned caps[]{3, 4, 6};
            item.sockets = item.nativeSeed % std::min(limit, caps[std::clamp(difficulty, 0, 2)]) + 1;
        }
    }
    if (item.sockets) item.nativeFlags |= 0x800;
    bool forcedEthereal=false;
    if(item.specialRow>=0) {
        const auto &records=item.quality==ItemQuality::Unique?data.uniqueItems:data.setItems;
        for(const auto &record:records) if(int(record.row)==item.specialRow)
            for(const auto &p:record.properties) forcedEthereal|=p.code=="ethereal";
    }
    const bool canEthereal=definition->maxDurability && !property("item_indesctructible") && !definition->questTag && item.quality!=ItemQuality::Inferior && item.quality!=ItemQuality::Set;
    if(forcedEthereal || (canEthereal && generation.rollNaturalEthereal && limitedRandom(local,100)<5)) {
        item.nativeFlags|=0x400000u;
        if(definition->family==ItemFamily::Armor) item.defense=item.defense*3/2;
    }
    const bool visible=item.identified;item.identified=true;
    freezeCubeItem(data,item,&local); // Preserve all rolls, including conditional set lists.
    item.nativeMaxDurability=std::min(255u,item.nativeMaxDurability*generation.durabilityMultiplier);
    innateProperties(data,*definition,item,local,staffmodBias);
    const auto complete=resolveItemStats(data,item,int(item.level));
    item.durability=std::min(255u,itemMaximumDurability(data,item,complete));
    updateCubeRequiredLevel(data,item);item.identified=visible;
    item.nativeQuestDifficulty = unsigned(difficulty);
    return item;
}
}
