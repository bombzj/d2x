#include "native_sources.hpp"
#include "definitions.hpp"
#include "state.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
namespace {
std::shared_ptr<StatList> leaf(StatSource source,StatOwnerKind owner) {
    auto result=std::make_shared<StatList>();result->source=source;result->ownerKind=owner;result->extended=false;return result;
}
int32_t baseWeaponDamage(const ItemInstance &item,int32_t base,int32_t inferiorMinimum) {
    if(item.nativeFlags&0x400000u) base=nativeMultiplyDivide(base,3,2);
    if(item.quality==ItemQuality::Inferior) base=std::max(inferiorMinimum,nativeMultiplyDivide(base,75,100));
    return base;
}
void appendProperties(StatList &part,const StatRuleCatalog &rules,std::span<const ResolvedItemStat> stats) {
    // Use original stat names and encoded rolls. Per-level op inputs must be
    // combined before evaluation, instead of summing rounded per-item outputs.
    for(const auto &stat:stats) {
        if(stat.layer<0 || stat.layer>UINT16_MAX) throw std::runtime_error("Invalid native property layer");
        addNativeStat(part,rules,stat.name,stat.rawValue,uint16_t(stat.layer));
    }
}
}
NativeEquipmentSources prepareNativeEquipmentSources(const ItemCatalog &items,const StatRuleCatalog &rules,
    std::span<const EquipmentContribution> contributions,EntityId owner,const NativeItemLookup &lookup,EntityId weapon,StatOwnerKind ownerKind,
    const std::map<EntityId,StatSnapshot> *itemFullStats) {
    NativeEquipmentSources result;
    auto &itemLists=result.items;
    std::map<std::string,std::shared_ptr<StatList>,std::less<>> setLists;
    for(const auto &contribution:contributions) {
        if(!contribution.item) {
            // All global property instructions of one set belong to the same
            // unit ITEMSET list. Property columns are not separate owners.
            auto &set=setLists[contribution.set];
            if(!set) {
                set=leaf({StatSourceKind::Set,owner,int(contribution.instruction),0},ownerKind);
                result.contributions.push_back(set);
            }
            appendProperties(*set,rules,contribution.stats);continue;
        }
        auto &list=itemLists[contribution.item];
        if(!list) {
            const auto &item=lookup(contribution.item);const auto *data=items.find(item.definition);
            if(!data) throw std::runtime_error("Missing native equipped item definition");
            list=std::make_shared<StatList>();list->source={StatSourceKind::Equipment,item.id,-1,0};
            list->ownerKind=StatOwnerKind::Item;list->unitRoot=list->hasOwner=list->hasUnit=true;
            list->handWeapon=contribution.weapon;
            list->excludeDamage=contribution.weapon && item.id!=weapon;
            if(data->family==ItemFamily::Armor) {
                addNativeStat(*list,rules,"armorclass",item.defense);
                if(data->base.block) addNativeStat(*list,rules,"toblock",*data->base.block);
            }
            if(data->equipment.isType("weap")) {
                const auto channelDamage=[&](int base,int minimum) {
                    return base?baseWeaponDamage(item,base,minimum):0;
                };
                // Pure two-handed weapons have blank one-hand damage columns.
                // Native item stats retain each channel independently; blanks
                // compile to zero and never borrow the other channel's damage.
                addNativeStat(*list,rules,"mindamage",channelDamage(data->base.minDamage.value_or(0),1));
                addNativeStat(*list,rules,"maxdamage",channelDamage(data->base.maxDamage.value_or(0),2));
                addNativeStat(*list,rules,"secondary_mindamage",channelDamage(data->base.twoHandMin.value_or(0),1));
                addNativeStat(*list,rules,"secondary_maxdamage",channelDamage(data->base.twoHandMax.value_or(0),2));
                if(data->base.throwMax.value_or(0)) {
                    // ItemsMagic preserves the asymmetric original 2/1 minima.
                    addNativeStat(*list,rules,"item_throw_mindamage",baseWeaponDamage(item,data->base.throwMin.value_or(0),2));
                    addNativeStat(*list,rules,"item_throw_maxdamage",baseWeaponDamage(item,*data->base.throwMax,1));
                }
            }
            if(item.grantedSkill>=0) {
                if(item.grantedSkill>UINT16_MAX) throw std::runtime_error("Invalid native starter skill");
                auto starter=leaf({StatSourceKind::Equipment,item.id,item.grantedSkill,0},StatOwnerKind::Item);
                addNativeStat(*starter,rules,"item_singleskill",1,uint16_t(item.grantedSkill));
                list->contributions.push_back(std::move(starter));
            }
            result.contributions.push_back(list);
        }
        auto part=leaf({contribution.kind==EquipmentContributionKind::Item?StatSourceKind::Equipment:StatSourceKind::Set,
            contribution.item,int(contribution.instruction),contribution.layer},StatOwnerKind::Item);
        appendProperties(*part,rules,contribution.stats);list->contributions.push_back(std::move(part));
    }
    if(itemFullStats) for(const auto &[id,list]:itemLists) if(const auto found=itemFullStats->find(id);found!=itemFullStats->end())
        list->fullValues=found->second.values;
    return result;
}
}
