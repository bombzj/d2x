#include "companion_content.hpp"
#include "item_content.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_magic_loot.hpp"
#include "content/items/item_properties.hpp"
#include "gameplay/items/equipment_stats.hpp"
#include "gameplay/items/equipment_contributions.hpp"
#include "gameplay/skills/amazon_summon_spec.hpp"
#include "gameplay/combat/attack_timing.hpp"
#include "resources/anim_data.hpp"
#include "resources/archive.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cctype>
namespace d2x {
namespace {
server::companions::Prepared prepare(const server::companions::Preparation &source,Archives &archives,const ClassicData &data) {
    server::companions::Prepared result;result.source=source;result.summon=source.summon;result.rule=source.rule;
    const auto &pet=*source.summon.amazon;auto random=source.seed;
    auto &stats=result.summon.stats;auto &attributes=stats.attributes;
    const int life=pet.lifeMinimum+int(limitedRandom(random,unsigned(std::max(0,pet.lifeMaximum-pet.lifeMinimum)+1)));
    attributes.maxLife=std::max(1,life*(100+pet.lifePercent)/100);
    auto &equipment=result.equipment;equipment.player.id=EntityId{1};equipment.player.characterClass=data.characters.at(size_t(pet.gfxClass)).code;
    equipment.player.level=stats.level;equipment.containers.equipment=EntityId{1};
    equipment.inventory.containers.emplace(EntityId{1},ContainerState{EntityId{1},{EntityId{1},ContainerKind::Equipment,int(EquipmentSlot::Count),1}});
    std::map<EquipmentSlot,ItemDefinition> definitions;EquipmentLoadout loadout;loadout.requirementPercent=[](const ItemInstance &){return 0;};
    loadout.maximumDurability=[&](const ItemInstance &item) {
        return itemMaximumDurability(data,item,resolveItemStats(data,item,stats.level));
    };
    for(const auto &entry:pet.equipment) {
        if(entry.rank>source.rank || definitions.contains(entry.slot)) continue;
        limitedRandom(random,1); // Native MonEquip alternatives, including a singleton.
        const auto *definition=data.items.find(entry.item);if(!definition) throw std::runtime_error("Missing Valkyrie MonEquip item");
        const auto generated=rollAffixItem(data,*definition,entry.quality,pet.itemLevel,random,"");
        if(!generated.deferred.empty()) throw std::runtime_error(generated.deferred);
        random=generated.randomState;
        auto item=prepareItem(data,{entry.item,1,{},unsigned(pet.itemLevel),generated.generation},random,source.difficulty);
        item.id=EntityId{equipment.inventory.items.size()+2};item.identified=true;item.requiredLevel=item.socketRequiredLevel=0;
        item.location=ContainerLocation{equipment.containers.equipment,{int(entry.slot),0}};
        auto &stored=equipment.inventory.items.emplace(item.id,std::move(item)).first->second;
        auto adjusted=*definition;adjusted.equipment.requiredClass.clear();adjusted.base.requiredStrength=adjusted.base.requiredDexterity=adjusted.base.requiredLevel=0;
        auto &rule=definitions.emplace(entry.slot,std::move(adjusted)).first->second;loadout.equipped[size_t(entry.slot)]={&stored,&rule};
    }
    const EquipmentActor base{"",attributes.strength,attributes.dexterity,stats.level};
    const EquipmentContributionSource contributions{{},[&](const ItemInstance &item,int level){return resolveItemStats(data,item,level);},{},{}};
    auto modifiers=deriveEquipmentModifiers(loadout,base,contributions);
    const EquipmentActor improved{"",attributes.strength+modifiers.strength,attributes.dexterity+modifiers.dexterity,stats.level};
    auto gearCombat=modifiers.combat;gearCombat.defensePercent+=attributes.combat.defensePercent;
    const auto gear=deriveEquipmentStats(loadout,improved,attributes.defense+modifiers.defense,gearCombat,attributes.attackRating+modifiers.attackRating);
    if(!gear.weapons[0].item) throw std::runtime_error("Valkyrie MonEquip has no weapon");
    attributes.strength=improved.strength;attributes.dexterity=improved.dexterity;
    attributes.defense=gear.defense;
    attributes.attackRating+=modifiers.attackRating;attributes.maxLife=std::max(1,attributes.maxLife*(100+modifiers.combat.lifePercent)/100+modifiers.maxLife);
    attributes.fireResist+=modifiers.fireResist;attributes.coldResist+=modifiers.coldResist;attributes.lightningResist+=modifiers.lightningResist;attributes.poisonResist+=modifiers.poisonResist;
    modifiers.combat.lifePercent=modifiers.combat.defensePercent=modifiers.combat.damagePercent=0;
    mergeCombatModifiers(attributes.combat,modifiers.combat);
    result.weapon=gear.weapons[0];result.weapon.meleeBaseMinimum+=int(stats.minimumDamage*256.f);result.weapon.meleeBaseMaximum+=int(stats.maximumDamage*256.f);
    // deriveEquipmentStats already includes both global and weapon-local AR.
    result.rule.level=stats.level;result.rule.attackRating=attributes.attackRating;result.rule.meleeRange=result.weapon.rangeAdder+1;
    result.rule.minimumDamage=result.weapon.meleeBaseMinimum/256;result.rule.maximumDamage=result.weapon.meleeBaseMaximum/256;
    result.rule.criticalChance=stats.critical;result.rule.difficulty=source.difficulty;
    const auto token=data.characters.at(size_t(pet.gfxClass)).appearance;
    const auto animation=data.skills.attackTimings.find(token+"a1"+gear.animationClass);
    if(animation==data.skills.attackTimings.end()) throw std::runtime_error("Missing original Valkyrie attack animation");
    const auto &a=animation->second;
    const WeaponAttackTiming timing{"a1",a.frames,effectiveAttackSpeed(a.speed,result.weapon.fasterAttack,result.weapon.baseSpeed,attributes.combat.attackRate),a.actionFrame,attackStartingFrame(equipment.player.characterClass,gear.animationClass,"a1")};
    result.rule.attackTicks=timing.durationTicks();result.rule.impactTick=timing.actionTick();result.rule.decisionTicks=std::max(1,pet.thinkFrames);
    // Native character death uses HTH (current MPQ AnimData: AMDTHTH).
    AnimDataTable animations(archives.read("data/global/animdata.d2"));auto death=token+"dthth";
    for(auto &ch:death) ch=char(std::toupper(static_cast<unsigned char>(ch)));
    const auto *dt=animations.find(death);if(!dt || dt->speed<=0) throw std::runtime_error("Missing original Valkyrie death animation");
    result.rule.deathTicks=std::max(1,int((dt->frames*256+dt->speed-1)/dt->speed));result.random=random;return result;
}
}
std::vector<std::string> preparePendingSummons(GameHost &host,GameHandle game,Archives &archives,const ClassicData &data) {
    std::vector<std::string> issues;
    for(const auto &source:host.pendingSummons(game)) {
        server::companions::Prepared result;
        try {result=prepare(source,archives,data);} catch(const std::exception &error) {result.source=source;result.deferred=error.what();}
        if(!result.deferred.empty()) issues.push_back("Summon " + std::to_string(source.skill) + ": " + result.deferred);
        host.installSummon(game,std::move(result));
    }
    return issues;
}
}
