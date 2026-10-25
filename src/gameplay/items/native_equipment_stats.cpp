#include "native_equipment_stats.hpp"
#include "definitions.hpp"
#include "state.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/stats/pd2/item_values.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
namespace {
std::array<int,3> mastery(const StatSnapshot &stats,const StatRuleCatalog &rules,const SkillFormulaCatalog &formulas,
    const EquipmentDefinition &item,std::string_view group,const WeaponDamage &weapon,const EquipmentStats &hands,bool dualWield) {
    if(rules.pd2) return pd2WeaponMastery(stats,rules,weapon,&hands,formulas,dualWield,group);
    std::array<int,3> result{};
    const std::array<std::string_view,3> suffixes{"th","dmg","crit"};
    for(size_t index=0;index<suffixes.size();++index) {
        const auto name=std::string("passive_mastery_")+std::string(group)+"_"+std::string(suffixes[index]);
        const auto id=rules.names.find(name);if(id==rules.names.end()) throw std::runtime_error("Missing native mastery definition");
        unsigned count=0;
        for(const auto &[key,value]:stats.values) if(key.id==id->second) {
            if(count++==32) break;
            const auto type=formulas.itemTypes.find(key.layer);
            if(type!=formulas.itemTypes.end() && item.isType(type->second)) result[index]=std::max(result[index],value);
        }
    }
    return result;
}
int32_t enhanced(int32_t base,int32_t percent) {return nativeAdd(base,nativePercentage(base,percent,100));}
}
EquipmentStats deriveNativeEquipmentStats(const EquipmentLoadout &loadout,const EquipmentActor &actor,const NativeEquipmentInput &input) {
    EquipmentStats result;result.level=actor.level;
    const auto &unarmed=input.hands.at(EntityId{});
    const auto common=[&](std::string_view name){return logicalStatValue(unarmed,input.rules,name);};
    const auto usable=[&](EquipmentSlot slot){return loadout.usable(slot,actor);};
    const auto *right=usable(weaponHandSlot(false,actor.weaponSet)),*left=usable(weaponHandSlot(true,actor.weaponSet));
    for(size_t index=0;index<size_t(EquipmentSlot::Count);++index) {
        if(!weaponSlotActive(EquipmentSlot(index),actor.weaponSet)) continue;
        const auto *item=usable(EquipmentSlot(index));if(!item) continue;
        const auto &definition=*loadout.find(item->id).definition;
        result.appearanceDefinitions[index]=definition.code;
        if(EquipmentSlot(index)==EquipmentSlot::Feet && definition.equipment.isType("boot"))
            result.kickBoots={true,definition.base.minDamage.value_or(0),definition.base.maxDamage.value_or(0),
                definition.base.strengthBonus.value_or(0),definition.base.dexterityBonus.value_or(0)};
        if(definition.equipment.isType("shld")) {
            result.shield=item->id;
            result.smiteMinimum=definition.base.minDamage.value_or(0);result.smiteMaximum=definition.base.maxDamage.value_or(0);
            result.shieldStrengthBonus=definition.base.strengthBonus.value_or(0);result.shieldDexterityBonus=definition.base.dexterityBonus.value_or(0);
            result.shieldTypes=definition.equipment.types;
        }
    }
    if(actor.hireling) {
        if(input.monsterBlock!=NativeEquipmentInput::MonsterBlock::Never &&
            (result.shield || input.monsterBlock==NativeEquipmentInput::MonsterBlock::Always)) result.blockChance=std::min(common("toblock"),75);
    } else if(result.shield) result.blockChance=unitBlockChance(nativeAdd(common("toblock"),actor.blockFactor),common("dexterity"),std::max(1,common("level")));
    result.defense=unitDefense(common("dexterity"),common("armorclass"),common("item_armor_percent"),common("skill_armor_percent"),
        result.shield?input.procedures.shieldDefensePercent:0,common("armor_override_percent"));
    // Parameterized PD2 mastery checks the opposite hand. Prepare both
    // identities before deriving either weapon's rating and damage.
    EquipmentStats hands;hands.shield=result.shield;
    for(const auto *item:{right,left}) if(item && loadout.find(item->id).definition->equipment.isType("weap")) {
        auto &hand=hands.weapons[item==left?1:0];hand.item=item->id;hand.leftHand=item==left;
    }
    const bool dualWield=actor.characterClass=="bar" || actor.characterClass=="ass";
    int count=0;
    for(const auto *item:{right,left}) {
        if(!item) continue;
        const auto &definition=*loadout.find(item->id).definition;
        if(!definition.equipment.isType("weap")) continue;
        const auto &stats=input.hands.at(item->id);
        const auto stat=[&](std::string_view name){return logicalStatValue(stats,input.rules,name);};
        const auto own=[&](std::string_view name){return logicalStatValue(input.items.at(item->id),input.rules,name);};
        const auto opposite=weaponHandSlot(item==right,actor.weaponSet);
        const bool twoHands=usesTwoHandedWeapon(definition.equipment,actor.characterClass,bool(loadout.equipped[size_t(opposite)]));
        auto &weapon=result.weapons.at(size_t(count++));
        weapon.twoHanded=twoHands;
        weapon.item=item->id;weapon.ranged=definition.equipment.isType("miss");weapon.throwable=definition.equipment.throwable;
        weapon.leftHand=item==left;weapon.potion=definition.equipment.isType("tpot");weapon.blunt=definition.equipment.isType("blun");
        weapon.types=definition.equipment.types;weapon.rangeAdder=definition.base.rangeAdder;weapon.hitClass=definition.base.hitClass;
        weapon.strengthBonus=definition.base.strengthBonus.value_or(0);weapon.dexterityBonus=definition.base.dexterityBonus.value_or(0);
        for(const auto &[id,type]:input.formulas.itemTypes) if(definition.equipment.isType(type)) weapon.typeLayers.push_back(id);
        weapon.weaponClass=definition.base.weaponClass;weapon.itemBaseSpeed=weapon.baseSpeed=definition.base.speed.value_or(0);
        weapon.fasterAttack=stat("item_fasterattackrate");weapon.itemAttackSpeed=own("item_fasterattackrate");
        weapon.itemAttackRate=own("attackrate");
        weapon.nativeModifiers=std::make_shared<const CombatModifiers>(nativeCombatModifiers(stats,input.rules,input.procedures));
        weapon.meleeMastery=mastery(stats,input.rules,input.formulas,definition.equipment,"melee",weapon,hands,dualWield);
        weapon.throwMastery=mastery(stats,input.rules,input.formulas,definition.equipment,"throw",weapon,hands,dualWield);
        weapon.masteryCritical=weapon.meleeMastery[2];
        const auto projectileMastery=weapon.throwable?weapon.throwMastery:weapon.meleeMastery;
        weapon.projectileMasteryCritical=projectileMastery[2];
        weapon.projectileMasteryRatingAdjustment=nativeSubtract(projectileMastery[0],weapon.meleeMastery[0]);
        weapon.statDamagePercent=stat("damagepercent");
        weapon.attributeDamagePercent=nativeAdd(nativeMultiplyDivide(definition.base.strengthBonus.value_or(0),stat("strength"),100),
            nativeMultiplyDivide(definition.base.dexterityBonus.value_or(0),stat("dexterity"),100));
        weapon.projectileAttributeDamagePercent=weapon.attributeDamagePercent;
        const auto bonus=nativeAdd(weapon.statDamagePercent,weapon.attributeDamagePercent);
        weapon.damagePercent=nativeAdd(bonus,weapon.meleeMastery[1]);
        weapon.minimumDamagePercent=stat("item_mindamage_percent");weapon.maximumDamagePercent=stat("item_maxdamage_percent");
        weapon.projectileDamagePercent=std::max(-90,nativeAdd(nativeAdd(bonus,projectileMastery[1]),std::max(weapon.minimumDamagePercent,weapon.maximumDamagePercent)));
        weapon.target=weapon.nativeModifiers->target;
        weapon.baseAttackRating=actor.hireling?nativeAdd(stat("tohit"),nativeMultiply(5,stat("dexterity"))):
            unitAttackRating(stat("dexterity"),stat("tohit"),input.attackRatingClassFactor);
        weapon.attackRatingPercent=nativeAdd(stat("item_tohit_percent"),weapon.meleeMastery[0]);
        weapon.attackRating=enhanced(weapon.baseAttackRating,weapon.attackRatingPercent);
        const auto low=nativeShiftLeft(stat(twoHands?"secondary_mindamage":"mindamage"),8);
        const auto high=nativeShiftLeft(stat(twoHands?"secondary_maxdamage":"maxdamage"),8);
        const auto normal=nativeShiftLeft(stat("item_normaldamage"),8);
        weapon.meleeBaseMinimum=nativeAdd(low,normal);weapon.meleeBaseMaximum=nativeAdd(high,normal);
        if(weapon.meleeBaseMinimum<1) weapon.meleeBaseMinimum=256;
        if(weapon.meleeBaseMaximum<=weapon.meleeBaseMinimum) weapon.meleeBaseMaximum=nativeAdd(weapon.meleeBaseMinimum,256);
        weapon.minimum=enhanced(weapon.meleeBaseMinimum,nativeAdd(std::max(-90,weapon.damagePercent),weapon.minimumDamagePercent));
        weapon.maximum=enhanced(weapon.meleeBaseMaximum,nativeAdd(std::max(-90,weapon.damagePercent),weapon.maximumDamagePercent));
        if(weapon.ranged || weapon.throwable) {
            if(!definition.base.projectile) throw std::runtime_error("Missing original weapon projectile");
            weapon.projectile=definition.base.projectile;
            // MISSILE_CalculateDamageData excludes ItemNormalDamage.
            weapon.projectileMinimum=weapon.throwable?nativeShiftLeft(stat("item_throw_mindamage"),8):low;
            weapon.projectileMaximum=weapon.throwable?nativeShiftLeft(stat("item_throw_maxdamage"),8):high;
            if(weapon.ranged) {
                weapon.minimum=enhanced(weapon.projectileMinimum,weapon.projectileDamagePercent);
                weapon.maximum=enhanced(weapon.projectileMaximum,weapon.projectileDamagePercent);
            }
            if(weapon.throwable && !weapon.potion) {
                weapon.throwMinimum=enhanced(weapon.projectileMinimum,weapon.projectileDamagePercent);
                weapon.throwMaximum=enhanced(weapon.projectileMaximum,weapon.projectileDamagePercent);
            }
        }
        result.animationClass=twoHands?definition.equipment.twoHandWeaponClass:weapon.weaponClass;
    }
    if(count) result.weaponCount=count;
    else {
        auto &fists=result.weapons[0];
        fists.nativeModifiers=std::make_shared<const CombatModifiers>(nativeCombatModifiers(unarmed,input.rules,input.procedures));
        const auto normal=nativeShiftLeft(common("item_normaldamage"),8);
        fists.meleeBaseMinimum=nativeAdd(nativeShiftLeft(std::max(1,common("mindamage")),8),normal);
        fists.meleeBaseMaximum=nativeAdd(nativeShiftLeft(std::max(2,common("maxdamage")),8),normal);
        if(fists.meleeBaseMinimum<1) fists.meleeBaseMinimum=256;
        if(fists.meleeBaseMaximum<=fists.meleeBaseMinimum) fists.meleeBaseMaximum=nativeAdd(fists.meleeBaseMinimum,256);
        fists.statDamagePercent=common("damagepercent");fists.attributeDamagePercent=common("strength");
        fists.projectileAttributeDamagePercent=actor.hireling?common("dexterity"):0;
        fists.projectileMinimum=nativeShiftLeft(common("mindamage"),8);fists.projectileMaximum=nativeShiftLeft(common("maxdamage"),8);
        fists.projectileDamagePercent=std::max(-90,nativeAdd(nativeAdd(fists.statDamagePercent,fists.projectileAttributeDamagePercent),
            std::max(common("item_mindamage_percent"),common("item_maxdamage_percent"))));
        fists.damagePercent=nativeAdd(fists.statDamagePercent,fists.attributeDamagePercent);
        fists.minimumDamagePercent=common("item_mindamage_percent");fists.maximumDamagePercent=common("item_maxdamage_percent");
        fists.minimum=enhanced(fists.meleeBaseMinimum,nativeAdd(std::max(-90,fists.damagePercent),fists.minimumDamagePercent));
        fists.maximum=enhanced(fists.meleeBaseMaximum,nativeAdd(std::max(-90,fists.damagePercent),fists.maximumDamagePercent));
        fists.baseAttackRating=actor.hireling?nativeAdd(common("tohit"),nativeMultiply(5,common("dexterity"))):
            unitAttackRating(common("dexterity"),common("tohit"),input.attackRatingClassFactor);
        fists.attackRatingPercent=common("item_tohit_percent");fists.attackRating=enhanced(fists.baseAttackRating,fists.attackRatingPercent);
        fists.fasterAttack=common("item_fasterattackrate");fists.target=fists.nativeModifiers->target;
    }
    if(count==2) {
        const auto &primary=result.weapons[0].weaponClass,&secondary=result.weapons[1].weaponClass;
        result.animationClass=primary=="ht1" && secondary=="ht1"?"ht2":primary=="1ht"?(secondary=="1ht"?"1jt":"1st"):(secondary=="1ht"?"1js":"1ss");
        const auto speed=nativeDivide(nativeAdd(result.weapons[0].baseSpeed,result.weapons[1].baseSpeed),2);
        for(auto &weapon:result.weapons) weapon.baseSpeed=speed;
    }
    if(input.rules.pd2) for(auto &weapon:result.weapons) if(weapon.nativeModifiers && weapon.nativeModifiers->pd2) {
        auto modifiers=*weapon.nativeModifiers;
        applyPd2WeaponParameters(*modifiers.pd2,input.hands.at(weapon.item),input.rules,weapon,&result,input.formulas,
            actor.characterClass=="bar" || actor.characterClass=="ass");
        weapon.nativeModifiers=std::make_shared<const CombatModifiers>(std::move(modifiers));
    }
    return result;
}
}
