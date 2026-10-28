#include "hireling_equipment.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/attributes/monster_sources.hpp"
#include "gameplay/items/native_sources.hpp"
#include "gameplay/items/inventory_order.hpp"
#include "gameplay/items/native_equipment_stats.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/skills/passive_stats.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace d2x::server::companions {
EquipmentLoadout hirelingLoadout(const PersistentCharacter &state, const ItemCatalog &catalog, const EquipmentRules &rules) {
    EquipmentLoadout loadout;
    const int level=state.player.hireling.level;
    loadout.requirementPercent=[&rules,level](const ItemInstance &item) {
        int32_t total=0;for(const auto &stat:rules.at(item.id,level).stats) if(!stat.layer && stat.effect=="item_req_percent") total=nativeAdd(total,stat.rawValue);
        return total;
    };
    loadout.maximumDurability=[&rules,level](const ItemInstance &item){return rules.at(item.id,level).maximumDurability;};
    for(const auto id:inventoryItemOrder(state.inventory)) {
        const auto &item=state.inventory.items.at(id);const auto *at=std::get_if<ContainerLocation>(&item.location);
        if(!at || at->container!=state.containers.hirelingEquipment || at->cell.x<0 || at->cell.x>=int(EquipmentSlot::Count)) continue;
        loadout.equipped[size_t(at->cell.x)]={&item,catalog.find(item.definition)};
    }
    return loadout;
}
HirelingEquipment calculateHirelingEquipment(const PersistentCharacter &state, const ItemCatalog &catalog,
    const EquipmentRules &rules, const PreparedHireling &prepared, EntityId excluded,const std::set<int> &states,EntityId unit,
    std::optional<Fixed8> currentLife) {
    if(!prepared.rule.stats || !prepared.rule.formulas) throw std::runtime_error("Missing native hireling calculation rules");
    const auto &statRules=*prepared.rule.stats;const auto &formulas=*prepared.rule.formulas;
    auto loadout=hirelingLoadout(state,catalog,rules);
    HirelingEquipment result;
    result.actor={"",prepared.strength,prepared.dexterity,state.player.hireling.level,0,0,true};
    result.actor.nativeClass=prepared.rule.nativeClass;
    std::vector<EquipmentContribution> contributions;
    EquipmentContributionSource source{rules.sets,
        [&](const ItemInstance &item,int level){return rules.at(item.id,level).stats;},
        [&](size_t set,size_t bonus,int level){return rules.setBonuses.at({set,bonus}).at(size_t(level));},
        [&](EntityId item,size_t index,int level){return rules.at(item,level).setStats.at(index);}, {},
        [&](EquipmentContribution contribution){contributions.push_back(std::move(contribution));}};
    std::set<EntityId> active;
    result.modifiers=deriveEquipmentModifiers(loadout,result.actor,source,excluded,&active);
    for(auto &item:loadout.equipped) if(item && !active.contains(item.instance->id)) item={};
    EntityId primary;
    for(const bool left:{false,true}) {
        const auto entry=loadout.equipped[size_t(weaponHandSlot(left,0))];
        if(entry && entry.definition->equipment.isType("weap")) {primary=entry.instance->id;break;}
    }
    auto equipment=prepareNativeEquipmentSources(catalog,statRules,contributions,unit,
        [&](EntityId id)->const ItemInstance & {return state.inventory.items.at(id);},primary,StatOwnerKind::Monster);
    auto root=std::make_shared<StatList>();root->ownerKind=StatOwnerKind::Monster;
    root->source={StatSourceKind::Base,unit,-1,0};root->hasOwner=root->unitRoot=root->hasAct=true;
    root->contributions=equipment.contributions;
    const auto base=[&](std::string_view name,int value){addNativeStat(*root,statRules,name,value);};
    base("level",state.player.hireling.level);base("strength",prepared.strength);base("dexterity",prepared.dexterity);
    base("alignment",2);base("monster_playercount",1);base("attackrate",100);base("velocitypercent",75);base("other_animrate",100);
    base("armorclass",prepared.rule.defense);base("tohit",prepared.rule.attackRating);base("toblock",prepared.rule.blockChance);
    // MONSTERAI_UpdateMercStats stores the hireling table damage in secondary
    // stats. Primary damage belongs to the monster mode list, not this base.
    base("secondary_mindamage",prepared.rule.minimumDamage);base("secondary_maxdamage",prepared.rule.maximumDamage);
    addNativeStat(*root,statRules,"maxhp",nativeShiftLeft(prepared.rule.minimumLife,8),0,true);
    base("hpregen",std::max(0,nativeShiftLeft(prepared.rule.minimumLife,8)/2000));
    const std::array<std::string_view,6> resistNames{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
    for(size_t i=0;i<resistNames.size();++i) base(resistNames[i],prepared.rule.resistances[i]);
    const auto ownerFull=evaluateStoredStatList(statRules,*root,{root.get(),{}, {}});
    for(const auto &[id,list]:equipment.items)
        list->fullValues=itemStatsAfterUnitChange(statRules,*list,nullptr,{root.get(),{}, {},root.get(),&ownerFull}).values;
    auto sampled=evaluateStatList(statRules,*root,{root.get(),{}, {}});
    result.skillRanks=nativeMonsterSkillRanks(prepared.baseSkillRanks,sampled,statRules,formulas,prepared.source.temporarySkillBonus);
    std::vector<ChargedSkill> charges;
    for(const auto &contribution:contributions) if(contribution.item) for(const auto &stat:contribution.stats)
        if(stat.name=="item_charged_skill") charges.push_back(decodeChargedSkill(state.inventory.items.at(contribution.item).handle(),stat.layer,stat.rawValue));
    SkillFormulaInput context{prepared.baseSkillRanks,result.skillRanks};
    context.statRules=&statRules;context.actor=root.get();context.stats=&sampled;
    context.referenceRanks=prepareSkillFormulaRanks(prepared.baseSkillRanks,result.skillRanks,{},charges,formulas.maximumSkillRank);
    if(currentLife) context.currentResources.emplace(StatKey{statRules.names.at("hitpoints"),0},currentLife->raw());
    else context.unavailableStats.insert("hitpoints");
    for(const auto &[id,rank]:result.skillRanks) {
        auto passive=std::make_shared<StatList>();passive->extended=false;passive->ownerKind=StatOwnerKind::Monster;
        passive->source={StatSourceKind::Passive,unit,id,0};root->contributions.push_back(passive);
        const auto refresh=[&]{sampled=evaluateStatList(statRules,*root,{root.get(),{}, {}});context.stats=&sampled;};
        refreshNativePassiveStats(formulas,statRules,id,rank,states,context,*passive,refresh);
    }
    result.nativeStats=evaluateStatList(statRules,*root,{root.get(),{}, {}});
    const auto stat=[&](std::string_view name){return logicalStatValue(result.nativeStats,statRules,name);};
    result.actor.strength=stat("strength");result.actor.dexterity=stat("dexterity");
    result.modifiers.combat=nativeCombatModifiers(result.nativeStats,statRules,result.modifiers.combat);
    result.modifiers.fireResist=nativeSubtract(stat("fireresist"),prepared.rule.resistances[2]);
    result.modifiers.lightningResist=nativeSubtract(stat("lightresist"),prepared.rule.resistances[3]);
    result.modifiers.coldResist=nativeSubtract(stat("coldresist"),prepared.rule.resistances[4]);
    result.modifiers.poisonResist=nativeSubtract(stat("poisonresist"),prepared.rule.resistances[5]);
    result.modifiers.fasterMoveVelocity=stat("item_fastermovevelocity");result.modifiers.otherAnimationRate=stat("other_animrate");
    std::map<EntityId,StatSnapshot> hands,items;
    const auto hand=[&](EntityId weapon) {
        auto selected=statListWithWeapon(*root,weapon,statRules,{root.get(),{}, {}});
        hands.emplace(weapon,evaluateStatList(statRules,*selected,{selected.get(),{}, {}}));
    };
    hand({});
    for(const auto &[id,list]:equipment.items) {
        items.emplace(id,evaluateStatList(statRules,*list,{root.get(),{}, {}}));
        if(list->handWeapon) hand(id);
    }
    const auto block=prepared.act==3?NativeEquipmentInput::MonsterBlock::Never:prepared.rule.blockWithoutShield?
        NativeEquipmentInput::MonsterBlock::Always:NativeEquipmentInput::MonsterBlock::RequireShield;
    result.equipment=deriveNativeEquipmentStats(loadout,result.actor,{statRules,hands,items,formulas,result.modifiers.combat,0,block});
    auto &weapon=result.equipment.weapons[0];
    if(!weapon.item) {
        weapon.ranged=prepared.act==1;weapon.weaponClass=weapon.ranged?"bow":"hth";
        result.equipment.animationClass=weapon.weaponClass;
        if(weapon.ranged) {
            weapon.minimum=nativeAdd(weapon.projectileMinimum,nativePercentage(weapon.projectileMinimum,weapon.projectileDamagePercent,100));
            weapon.maximum=nativeAdd(weapon.projectileMaximum,nativePercentage(weapon.projectileMaximum,weapon.projectileDamagePercent,100));
        }
    }
    result.life=Fixed8::fromRaw(nativeStatValue(result.nativeStats,statRules,"maxhp"));
    if(result.life<=0) throw std::runtime_error("Native hireling maximum life cannot be admitted");
    result.statSources=statListWithFullValues(*root,evaluateStoredStatList(statRules,*root,{root.get(),{}, {}}));return result;
}
void prepareHirelingEquipmentSources(monsters::Actor &candidate,const HirelingEquipment &equipment,uint64_t tick,
    std::span<const ModifierStatSource> effects,bool initial,const attributes::MonsterAuraMutation &mutation) {
    if(!candidate.nativeStats || !candidate.rule.stats || !equipment.statSources) throw std::runtime_error("Missing hireling source candidate");
    const auto &rules=*candidate.rule.stats;
    auto target=statListWithOwner(*equipment.statSources,candidate.id);
    if(initial) {
        candidate.nativeCharges.clear();candidate.nativeChargesTracked=true;
        auto base=std::make_shared<StatList>(*target);base->contributions.clear();base->fullValues.reset();
        candidate.nativeStats=NativeUnitStats{base,evaluateStatList(rules,*base,{base.get(),{}, {}})};
    }
    attributes::MonsterResourceTrace resources(candidate,tick);
    candidate.nativeChargesTracked=true;
    resources.sampleSources(effects);
    resources.observeSourceWrites([&](StatValue write,EntityId item,const StatSnapshot &stats,std::shared_ptr<const StatList> root) {
        attributes::dispatchMonsterItemAura(candidate,resources,write,item,stats,std::move(root),mutation);
    });
    // Base table changes precede inventory updates. Compare each raw base key,
    // allowing native max-resource and reverse-op callbacks at every write.
    auto oldBase=resources.read().intrinsic->sources->base;
    std::set<StatKey> keys;for(const auto &[key,value]:oldBase) keys.insert(key);
    for(const auto &[key,value]:target->base) keys.insert(key);
    for(const auto key:keys) {
        if(key.layer) throw std::runtime_error("Parameterized hireling base source requires its native writer");
        const auto found=target->base.find(key);const int32_t value=found==target->base.end()?0:found->second;
        if(!oldBase.contains(key) || oldBase.at(key)!=value) resources.writeBaseStat(rules.definitions.at(key.id).name,value,effects);
    }
    const auto same=[](const StatSource &a,const StatSource &b) {
        return a.kind==b.kind && a.owner==b.owner && a.definition==b.definition && a.occurrence==b.occurrence;
    };
    const auto old=resources.read().intrinsic->sources->contributions;
    for(const auto &part:old) if(part && (part->source.kind==StatSourceKind::Equipment || part->source.kind==StatSourceKind::Set) &&
        std::none_of(target->contributions.begin(),target->contributions.end(),[&](const auto &next){return next && same(next->source,part->source);}))
        resources.withdrawIntrinsicSource(part->source,effects);
    for(const auto &part:target->contributions) {
        if(!part) continue;
        if(part->source.kind==StatSourceKind::Equipment || part->source.kind==StatSourceKind::Set) {
            auto next=std::make_shared<StatList>(*part);
            const auto root=resources.read().intrinsic->sources;
            const auto current=resources.values(effects);
            next->fullValues=itemStatsAfterUnitChange(rules,*next,nullptr,{root.get(),{}, {},root.get(),&current}).values;
            resources.replaceIntrinsicSource(std::move(next),effects);
        }
    }
    // Inventory skill callbacks replace passive lists after equipment writes.
    for(const auto &part:old) if(part && part->source.kind==StatSourceKind::Passive &&
        std::none_of(target->contributions.begin(),target->contributions.end(),[&](const auto &next){return next && same(next->source,part->source);}))
        resources.withdrawIntrinsicSource(part->source,effects);
    for(const auto &part:target->contributions) if(part && part->source.kind==StatSourceKind::Passive)
        resources.replaceIntrinsicSource(part,effects);
    const auto change=resources.take();
    candidate.nativeStats=change.intrinsic;candidate.life=change.life.raw();candidate.maximumLife=change.maximum.raw();
}

monsters::Actor prepareHirelingBody(const monsters::Actor &live,const PersistentCharacter &owner,const PreparedHireling &prepared,
    const ItemCatalog &catalog,const EquipmentRules &rules,uint64_t inventoryRevision,uint64_t characterRevision,
    const std::set<int> &states,std::span<const ModifierStatSource> effects,uint64_t tick,bool initial,const attributes::MonsterAuraMutation &mutation) {
    if(live.revision==UINT64_MAX) throw std::overflow_error("Hireling revision exhausted");
    const auto stats=calculateHirelingEquipment(owner,catalog,rules,prepared,{},states,live.id,Fixed8::fromRaw(nativeChecked(live.life)));
    auto candidate=live;const bool levelUp=prepared.rule.level>live.rule.level && live.alive() && live.life>0;
    candidate.rule=prepared.rule;
    candidate.rule.statGameType=live.rule.statGameType;
    candidate.rule.minimumLife=candidate.rule.maximumLife=std::max(1,stats.life.raw()>>8);
    candidate.rule.defense=stats.equipment.defense;candidate.rule.attackRating=stats.equipment.weapons[0].attackRating;
    candidate.rule.minimumDamage=stats.equipment.weapons[0].minimum/256;candidate.rule.maximumDamage=stats.equipment.weapons[0].maximum/256;
    candidate.rule.blockChance=stats.equipment.blockChance;candidate.rule.blockWithoutShield=bool(stats.equipment.shield);
    candidate.petWeapon=stats.equipment.weapons[0];candidate.petStats.attributes.combat=stats.modifiers.combat;
    candidate.petStats.block=candidate.rule.blockChance;
    const std::array<std::string_view,6> resistNames{"damageresist","magicresist","fireresist","lightresist","coldresist","poisonresist"};
    for(size_t index=0;index<resistNames.size();++index)
        candidate.rule.resistances[index]=logicalStatValue(stats.nativeStats,*candidate.rule.stats,resistNames[index]);
    candidate.hirelingStrength=stats.actor.strength;candidate.hirelingDexterity=stats.actor.dexterity;
    candidate.hirelingVitality=std::max(0,stats.modifiers.vitality);
    auto gear=std::make_shared<PersistentCharacter>(owner);
    std::erase_if(gear->inventory.items,[&](const auto &entry) {
        const auto *at=std::get_if<ContainerLocation>(&entry.second.location);
        return !at || at->container!=owner.containers.hirelingEquipment;
    });
    candidate.equipment=std::move(gear);candidate.hirelingInventoryRevision=inventoryRevision;candidate.hirelingCharacterRevision=characterRevision;
    prepareHirelingEquipmentSources(candidate,stats,tick,effects,initial,mutation);
    if(levelUp) candidate.life=candidate.maximumLife;
    unsigned passiveMask=0;
    for(size_t index=0;index<prepared.passives.size();++index)
        if(states.contains(prepared.passives[index].first.suppressedByState)) passiveMask|=1u<<index;
    const int frw=std::max(0,stats.modifiers.fasterMoveVelocity);
    candidate.hirelingPassiveMask=passiveMask;candidate.hirelingNextExperience=prepared.nextExperience;
    candidate.hirelingMovementPercent=stats.modifiers.velocityPercent+150*frw/(150+frw);
    candidate.hirelingBaseAttributes={{"maxhp",int64_t(prepared.rule.minimumLife)*256},{"strength",prepared.strength},{"dexterity",prepared.dexterity},
        {"armorclass",prepared.rule.defense},{"mindamage",prepared.rule.minimumDamage},{"maxdamage",prepared.rule.maximumDamage},
        {"fireresist",prepared.rule.resistances[2]},{"lightresist",prepared.rule.resistances[3]},
        {"coldresist",prepared.rule.resistances[4]},{"poisonresist",prepared.rule.resistances[5]}};
    ++candidate.revision;return candidate;
}

}
