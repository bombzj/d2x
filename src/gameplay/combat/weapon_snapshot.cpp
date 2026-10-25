#include "weapon_snapshot.hpp"
#include "unit_values.hpp"
#include "gameplay/stats/modifier_values.hpp"
#include "gameplay/stats/pd2/item_values.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
#include <stdexcept>
namespace d2x {
WeaponDamage sampleNativeWeapon(const WeaponDamage &input,const StatSnapshot &values,const StatRuleCatalog &rules,
    const CombatModifiers &procedures,bool monster,int classFactor,const EquipmentStats *equipment,
    const SkillFormulaCatalog *formulas,bool dualWield) {
    auto result=input;
    const auto stat=[&](std::string_view name){return logicalStatValue(values,rules,name);};
    const auto mastery=[&](std::string_view group) {
        if(rules.pd2) {
            if(!formulas) throw std::logic_error("PD2 weapon mastery requires item type metadata");
            return pd2WeaponMastery(values,rules,input,equipment,*formulas,dualWield,group);
        }
        std::array<int,3> result{};
        const std::array<std::string_view,3> suffixes{"th","dmg","crit"};
        for(size_t index=0;index<suffixes.size();++index) {
            const auto name=std::string("passive_mastery_")+std::string(group)+"_"+std::string(suffixes[index]);
            const auto id=rules.names.find(name);if(id==rules.names.end()) throw std::runtime_error("Missing native mastery stat");
            unsigned count=0;
            for(const auto &[key,value]:values.values) if(key.id==id->second) {
                if(count++==32) break;
                if(std::find(input.typeLayers.begin(),input.typeLayers.end(),key.layer)!=input.typeLayers.end()) result[index]=std::max(result[index],value);
            }
        }
        return result;
    };
    auto modifiers=nativeCombatModifiers(values,rules,procedures);
    if(modifiers.pd2 && formulas) applyPd2WeaponParameters(*modifiers.pd2,values,rules,input,equipment,*formulas,dualWield);
    result.nativeModifiers=std::make_shared<const CombatModifiers>(std::move(modifiers));
    result.meleeMastery=mastery("melee");result.throwMastery=mastery("throw");
    result.statDamagePercent=stat("damagepercent");
    result.attributeDamagePercent=input.item?nativeAdd(nativeMultiplyDivide(input.strengthBonus,stat("strength"),100),
        nativeMultiplyDivide(input.dexterityBonus,stat("dexterity"),100)):stat("strength");
    result.projectileAttributeDamagePercent=input.item?result.attributeDamagePercent:monster?stat("dexterity"):0;
    result.damagePercent=nativeAdd(nativeAdd(result.statDamagePercent,result.attributeDamagePercent),result.meleeMastery[1]);
    result.minimumDamagePercent=stat("item_mindamage_percent");result.maximumDamagePercent=stat("item_maxdamage_percent");
    const auto projectileMastery=input.throwable?result.throwMastery:result.meleeMastery;
    result.projectileDamagePercent=std::max(-90,nativeAdd(nativeAdd(nativeAdd(result.statDamagePercent,result.projectileAttributeDamagePercent),projectileMastery[1]),
        std::max(result.minimumDamagePercent,result.maximumDamagePercent)));
    result.baseAttackRating=monster?nativeAdd(stat("tohit"),nativeMultiply(stat("dexterity"),5)):unitAttackRating(stat("dexterity"),stat("tohit"),classFactor);
    result.attackRatingPercent=nativeAdd(stat("item_tohit_percent"),result.meleeMastery[0]);
    result.attackRating=nativeAdd(result.baseAttackRating,nativePercentage(result.baseAttackRating,result.attackRatingPercent,100));
    result.target=result.nativeModifiers->target;result.fasterAttack=stat("item_fasterattackrate");
    result.masteryCritical=result.meleeMastery[2];result.projectileMasteryCritical=projectileMastery[2];
    result.projectileMasteryRatingAdjustment=nativeSubtract(projectileMastery[0],result.meleeMastery[0]);
    auto minimum=stat(input.twoHanded?"secondary_mindamage":"mindamage"),maximum=stat(input.twoHanded?"secondary_maxdamage":"maxdamage");
    if(!input.item) {minimum=std::max(minimum,1);maximum=std::max(maximum,2);}
    const auto normal=nativeShiftLeft(stat("item_normaldamage"),8);
    result.meleeBaseMinimum=nativeAdd(nativeShiftLeft(minimum,8),normal);result.meleeBaseMaximum=nativeAdd(nativeShiftLeft(maximum,8),normal);
    if(result.meleeBaseMinimum<1) result.meleeBaseMinimum=256;
    if(result.meleeBaseMaximum<=result.meleeBaseMinimum) result.meleeBaseMaximum=nativeAdd(result.meleeBaseMinimum,256);
    result.projectileMinimum=nativeShiftLeft(stat(input.throwable?"item_throw_mindamage":input.twoHanded?"secondary_mindamage":"mindamage"),8);
    result.projectileMaximum=nativeShiftLeft(stat(input.throwable?"item_throw_maxdamage":input.twoHanded?"secondary_maxdamage":"maxdamage"),8);
    const auto enhanced=[](int base,int percent){return nativeAdd(base,nativePercentage(base,percent,100));};
    result.minimum=enhanced(result.meleeBaseMinimum,nativeAdd(std::max(-90,result.damagePercent),result.minimumDamagePercent));
    result.maximum=enhanced(result.meleeBaseMaximum,nativeAdd(std::max(-90,result.damagePercent),result.maximumDamagePercent));
    if(result.ranged) {result.minimum=enhanced(result.projectileMinimum,result.projectileDamagePercent);result.maximum=enhanced(result.projectileMaximum,result.projectileDamagePercent);}
    if(result.throwable) {result.throwMinimum=enhanced(result.projectileMinimum,result.projectileDamagePercent);result.throwMaximum=enhanced(result.projectileMaximum,result.projectileDamagePercent);}
    return result;
}
}
