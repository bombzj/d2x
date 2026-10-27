#include "combat_display.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/equipment_combat.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/items/native_equipment_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/passive.hpp"
#include "gameplay/skills/barbarian_passive_spec.hpp"
#include "gameplay/skills/amazon_passive_spec.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
CombatDisplayEquipment projectCombatEquipment(const ClassicData &data,EquipmentActor actor,
    std::span<const CombatDisplayItem> items,std::span<const ItemInstance> charms,
    std::span<const ResolvedItemStat> states,const std::map<int,int> &ranks,const std::map<int,int> &baseRanks,
    const std::set<int> &activeStates,const std::set<int> &accountedPassives,int baseDefense,int baseRating,int baseMinimum,int baseMaximum) {
    EquipmentLoadout loadout;
    loadout.requirementPercent=[&](const ItemInstance &item) {
        int value=0;for(const auto &stat:resolveItemStats(data,item,actor.level))
            if(stat.effect=="item_req_percent" && !stat.layer) value+=stat.value;
        return value;
    };
    loadout.maximumDurability=[&](const ItemInstance &item) {
        const auto stats=resolveItemStats(data,item,actor.level);
        return itemMaximumDurability(data,item,stats);
    };
    for(const auto &entry:items) loadout.equipped.at(size_t(entry.slot))={&entry.item,data.items.find(entry.item.definition)};
    for(const auto &item:charms) loadout.backpack.push_back({&item,data.items.find(item.definition)});
    EquipmentContributionSource source{data.equipmentSets,
        [&](const ItemInstance &item,int level){return resolveItemStats(data,item,level);},
        [&](size_t set,size_t bonus,int level) {
            const auto &property=data.setItems.at(set).setBonuses.at(bonus).property;
            return resolvePropertyStats(data,property,property.minimum.value_or(0),level);
        },
        [&](EntityId id,size_t layer,int level) {
            const auto *item=loadout.find(id).instance;
            if(!item) throw std::runtime_error("Missing observed set item");
            auto copy=*item;copy.savedStats=copy.savedSetStats.at(layer);copy.savedSetStats={};
            copy.runewordStats.clear();copy.socketedItems.clear();
            return resolveOwnItemStats(data,copy,level);
        }};
    std::vector<EquipmentContribution> contributions;
    source.contribution=[&](EquipmentContribution value){contributions.push_back(std::move(value));};
    auto mods=deriveEquipmentModifiers(loadout,actor,source);
    for(const auto &stat:states) {
        if(stat.layer) continue;
        applyEquipmentStat(stat,{},false,mods.combat);
        if(stat.effect=="skill_armor_percent") mods.combat.defensePercent+=stat.value;
        if(stat.effect=="armor_override_percent") mods.combat.armorOverridePercent+=stat.value;
        if(stat.effect=="armorclass") mods.defense+=stat.value;
        if(stat.effect=="tohit") mods.attackRating+=stat.value;
        if(stat.effect=="strength") mods.strength+=stat.value;
        if(stat.effect=="dexterity") mods.dexterity+=stat.value;
        if(stat.effect=="maxhp") mods.maxLife+=stat.value/256;
        if(stat.effect=="fireresist") mods.fireResist+=stat.value;
        if(stat.effect=="coldresist") mods.coldResist+=stat.value;
        if(stat.effect=="lightresist") mods.lightningResist+=stat.value;
        if(stat.effect=="poisonresist") mods.poisonResist+=stat.value;
    }
    for(const auto &[id,rank]:ranks) {
        const auto *skill=data.skills.find(id);if(!skill || rank<=0) continue;
        // Original states carrying passive values are already included above.
        const bool suppressed=activeStates.contains(skill->passiveSuppressedByState);
        const auto base=baseRanks.find(id);
        if(!accountedPassives.contains(id)) {
            applySkillPassive(mods,skill->passiveContribution,base==baseRanks.end()?0:base->second,suppressed);
            if(const auto &amazon=skill->passiveContribution.amazon;amazon && amazon->stat==AmazonPassiveStat::Rating)
                mods.combat.attackRatingPercent+=amazonPassiveValue(*amazon,rank);
        }
        if(const auto &barbarian=skill->passiveContribution.barbarian;barbarian && !suppressed &&
            (barbarian->kind==BarbarianPassiveSpec::Kind::Mastery || !accountedPassives.contains(id)))
            applyBarbarianPassive(mods,*barbarian,rank);
    }
    if(actor.hireling) {actor.strength+=mods.strength;actor.dexterity+=mods.dexterity;}
    auto combat=mods.combat;
    combat.minimumDamage+=baseMinimum;combat.maximumDamage+=baseMaximum;
    const int rating=baseRating+5*actor.dexterity+mods.attackRating;
    auto equipment=deriveEquipmentStats(loadout,actor,baseDefense+mods.defense,combat,rating);
    if(data.profile=="lod-named-txt-v1" && !actor.hireling && data.nativeStats && data.nativeFormulas) {
        const auto &rules=*data.nativeStats;
        auto sources=prepareNativeEquipmentSources(data.items,rules,contributions,{},
            [&](EntityId id)->const ItemInstance &{
                for(const auto &item:charms) if(item.id==id) return item;
                const auto entry=loadout.find(id);
                if(!entry) throw std::runtime_error("Missing observed native equipment source");
                return *entry.instance;
            });
        StatList root;root.ownerKind=StatOwnerKind::Player;root.unitRoot=root.hasOwner=root.hasUnit=true;
        root.contributions=std::move(sources.contributions);
        addNativeStat(root,rules,"level",actor.level);
        addNativeStat(root,rules,"strength",actor.strength-mods.strength);
        addNativeStat(root,rules,"dexterity",actor.dexterity-mods.dexterity);
        addNativeStat(root,rules,"tohit",baseRating);
        addNativeStat(root,rules,"mindamage",baseMinimum);
        addNativeStat(root,rules,"maxdamage",baseMaximum);
        auto observed=std::make_shared<StatList>();observed->extended=false;
        for(const auto &stat:states)
            addNativeStat(*observed,rules,stat.name,stat.rawValue,uint16_t(stat.layer),true);
        root.contributions.push_back(std::move(observed));
        std::map<EntityId,StatSnapshot> hands,itemStats;
        StatQueryContext context;context.unit=&root;
        const auto character=std::find_if(data.characters.begin(),data.characters.end(),
            [&](const auto &definition){return definition.code==actor.characterClass;});
        if(character==data.characters.end()) throw std::runtime_error("Missing observed native character definition");
        context.character=StatCharacterContext{character->lifePerVitality,character->manaPerEnergy,
            character->staminaPerVitality,rules.names.at("maxstamina")};
        for(const auto &[id,list]:sources.items) itemStats.emplace(id,evaluateStatList(rules,*list,context));
        const auto sample=[&](EntityId id) {
            const auto selected=statListWithWeapon(root,id,rules,context);
            auto handContext=context;handContext.unit=selected.get();
            hands.emplace(id,evaluateStatList(rules,*selected,handContext));
        };
        sample({});
        for(int index=0;index<equipment.weaponCount;++index)
            if(equipment.weapons[index].item) sample(equipment.weapons[index].item);
        const auto native=deriveNativeEquipmentStats(loadout,actor,
            {rules,hands,itemStats,*data.nativeFormulas,combat,35});
        equipment.weapons=native.weapons;
    }
    if(actor.hireling && !equipment.weapons[0].item) {
        auto &weapon=equipment.weapons[0];
        weapon.meleeBaseMinimum=std::max(0,combat.minimumDamage)*256;
        weapon.meleeBaseMaximum=std::max(combat.minimumDamage,combat.maximumDamage)*256;
        const int percent=std::max(-90,actor.strength+combat.damagePercent+std::max(combat.minimumDamagePercent,combat.maximumDamagePercent));
        weapon.minimum=int(int64_t(weapon.meleeBaseMinimum)*(100+percent)/100);
        weapon.maximum=int(int64_t(weapon.meleeBaseMaximum)*(100+percent)/100);
    }
    return {std::move(mods),std::move(equipment),std::move(actor)};
}
} // namespace d2x
