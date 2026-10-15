#include "damage_data.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

namespace d2x {
namespace {
int32_t stat(const StatSnapshot &values,const StatRuleCatalog &rules,std::string_view name) {
    return values.get({rules.names.at(std::string(name)),0});
}
void addRange(MissileDamageRange &range,const StatSnapshot &values,const StatRuleCatalog &rules,
    std::string_view minimum,std::string_view maximum,uint8_t shift) {
    range.minimum=nativeAdd(range.minimum,nativeShiftLeft(stat(values,rules,minimum),shift));
    range.maximum=nativeAdd(range.maximum,nativeShiftLeft(stat(values,rules,maximum),shift));
}
}
bool missileHasBonusStats(int32_t critical,int32_t deadly,bool weapon,int32_t mastery,uint64_t &random) {
    if(int32_t(rollRandom(random)%100)<critical) return true;
    if(deadly && int32_t(rollRandom(random)%100)<deadly) return true;
    return weapon && mastery && int32_t(rollRandom(random)%100)<mastery;
}
void addMissileDamageStats(MissileDamageData &data,const StatSnapshot &values,const StatRuleCatalog &rules,uint8_t shift) {
    addRange(data.fire,values,rules,"firemindam","firemaxdam",shift);
    addRange(data.lightning,values,rules,"lightmindam","lightmaxdam",shift);
    addRange(data.magic,values,rules,"magicmindam","magicmaxdam",shift);
    addRange(data.cold,values,rules,"coldmindam","coldmaxdam",shift);
    data.coldLength=nativeAdd(data.coldLength,stat(values,rules,"coldlength"));
    addRange(data.poison,values,rules,"poisonmindam","poisonmaxdam",0);
    const auto overrideLength=stat(values,rules,"skill_poison_override_length");
    if(overrideLength<=0) {
        data.poisonLength=nativeAdd(data.poisonLength,stat(values,rules,"poisonlength"));
        data.poisonCount=nativeAdd(data.poisonCount,stat(values,rules,"poison_count"));
    } else data.poisonLength=nativeAdd(data.poisonLength,overrideLength);
    data.lifeDrain.minimum=nativeAdd(data.lifeDrain.minimum,stat(values,rules,"lifedrainmindam"));
    data.manaDrain.minimum=nativeAdd(data.manaDrain.minimum,stat(values,rules,"manadrainmindam"));
    addRange(data.burn,values,rules,"burningmin","burningmax",0);
    data.burnLength=nativeAdd(data.burnLength,stat(values,rules,"firelength"));
}
void scaleMissileDamageElements(MissileDamageData &data,int32_t sourceDamage) {
    if(sourceDamage==128) return;
    const auto scale=[&](int32_t &value){value=nativeMultiplyDivide(value,sourceDamage,128);};
    for(auto *range:{&data.fire,&data.lightning,&data.magic,&data.cold,&data.poison,&data.burn}) {
        scale(range->minimum);scale(range->maximum);
    }
    scale(data.coldLength);scale(data.poisonCount);scale(data.lifeDrain.minimum);scale(data.manaDrain.minimum);scale(data.burnLength);
}
MissileDamageData calculateMissileDamageData(const MissileDamageInput &input,const StatRuleCatalog &rules) {
    MissileDamageData data;data.flags=(uint32_t(input.holy)&7u)<<8;
    const auto read=[&](std::string_view name){return stat(input.owner,rules,name);};
    int32_t sourceDamage=input.unitSourceDamage,damagePercent{},minimumPercent{},maximumPercent{};
    MissileDamageRange base;
    if(sourceDamage) {
        data.flags|=1;if(input.bonusStats) data.flags|=2;
        int32_t bonus{};
        if(input.weapon) {
            const auto &weapon=*input.weapon;
            if(input.halfTwoHanded && weapon.twoHanded) sourceDamage=nativeDivide(sourceDamage,2);
            bonus=weapon.masteryDamage;
            if(weapon.strengthBonus) bonus=nativeAdd(bonus,nativeMultiplyDivide(read("strength"),weapon.strengthBonus,100));
            if(weapon.dexterityBonus) bonus=nativeAdd(bonus,nativeMultiplyDivide(read("dexterity"),weapon.dexterityBonus,100));
            base=weapon.throwable?MissileDamageRange{nativeShiftLeft(read("item_throw_mindamage"),8),nativeShiftLeft(read("item_throw_maxdamage"),8)}:
                weapon.dualWield?MissileDamageRange{nativeShiftLeft(read("secondary_mindamage"),8),nativeShiftLeft(read("secondary_maxdamage"),8)}:
                MissileDamageRange{nativeShiftLeft(read("mindamage"),8),nativeShiftLeft(read("maxdamage"),8)};
            if(weapon.blunt) data.undeadPercent=nativeAdd(data.undeadPercent,50);
        } else {
            if(input.hireling) damagePercent=read("dexterity");
            base={nativeShiftLeft(read("mindamage"),8),nativeShiftLeft(read("maxdamage"),8)};
        }
        damagePercent=nativeAdd(damagePercent,nativeAdd(bonus,read("damagepercent")));
        minimumPercent=read("item_mindamage_percent");maximumPercent=read("item_maxdamage_percent");
        data.demonPercent=nativeAdd(data.demonPercent,read("item_demondamage_percent"));
        data.undeadPercent=nativeAdd(data.undeadPercent,read("item_undeaddamage_percent"));data.targetArmor=read("item_damagetargetac");
        if(read("skill_bypass_undead")) data.flags|=0x100;
        if(read("skill_bypass_demons")) data.flags|=0x200;
        if(read("skill_bypass_beasts")) data.flags|=0x400;
    } else if(input.originSourceDamage && input.origin) {
        sourceDamage=input.originSourceDamage;if(input.bonusStats) data.flags|=2;
        const auto origin=[&](std::string_view name){return stat(*input.origin,rules,name);};
        base={origin("mindamage"),origin("maxdamage")};damagePercent=origin("damagepercent");
        minimumPercent=origin("item_mindamage_percent");maximumPercent=origin("item_maxdamage_percent");
        // Retail 1.13c RVA 6BFD4..6C03B adds demon once. D2MOO's
        // duplicated demon addition is a reference transcription error.
        data.demonPercent=origin("item_demondamage_percent");data.undeadPercent=origin("item_undeaddamage_percent");
        data.targetArmor=origin("item_damagetargetac");
    }
    data.damagePercent=std::max(-90,nativeAdd(std::max(minimumPercent,maximumPercent),
        nativeAdd(stat(input.missile,rules,"damagepercent"),damagePercent)));
    data.physical={nativeAdd(input.physical.minimum,nativeMultiplyDivide(base.minimum,sourceDamage,128)),
        nativeAdd(input.physical.maximum,nativeMultiplyDivide(base.maximum,sourceDamage,128))};
    switch(input.element) {
    case MissileDamageElement::Fire:data.fire=input.elemental;data.fireLength=input.length;break;
    case MissileDamageElement::Lightning:data.lightning=input.elemental;break;
    case MissileDamageElement::Magic:data.magic=input.elemental;break;
    case MissileDamageElement::Cold:case MissileDamageElement::Freeze:data.cold=input.elemental;data.coldLength=input.length;break;
    case MissileDamageElement::Poison:data.poison=input.elemental;data.poisonLength=input.length;break;
    case MissileDamageElement::LifeDrain:data.lifeDrain=input.elemental;break;
    case MissileDamageElement::ManaDrain:data.manaDrain=input.elemental;break;
    case MissileDamageElement::StaminaDrain:data.staminaDrain=input.elemental;break;
    case MissileDamageElement::Stun:data.stunLength=input.length;break;
    case MissileDamageElement::Burn:data.burn=input.elemental;data.burnLength=input.length;break;
    case MissileDamageElement::None:case MissileDamageElement::Random:break;
    }
    if(input.unitSourceDamage && sourceDamage) {
        addMissileDamageStats(data,input.owner,rules,8);scaleMissileDamageElements(data,sourceDamage);
    } else if(input.originSourceDamage && input.origin) {
        addMissileDamageStats(data,*input.origin,rules,0);scaleMissileDamageElements(data,input.originSourceDamage);
    }
    return data;
}
std::vector<StatValue> missileDamageStatWrites(const MissileDamageData &data,int32_t level,const StatSnapshot &owner,const StatRuleCatalog &rules) {
    std::vector<StatValue> writes;
    const auto write=[&](std::string_view name,int32_t value){writes.emplace_back(StatKey{rules.names.at(std::string(name)),0},value);};
    const auto range=[&](std::string_view minimum,std::string_view maximum,MissileDamageRange value){write(minimum,value.minimum);write(maximum,value.maximum);};
    write("level",level);range("mindamage","maxdamage",data.physical);
    range("firemindam","firemaxdam",data.fire);write("firelength",data.fireLength);
    range("lightmindam","lightmaxdam",data.lightning);range("magicmindam","magicmaxdam",data.magic);
    range("coldmindam","coldmaxdam",data.cold);write("coldlength",data.coldLength);
    range("poisonmindam","poisonmaxdam",data.poison);write("poisonlength",data.poisonLength);write("poison_count",data.poisonCount);
    range("lifedrainmindam","lifedrainmaxdam",data.lifeDrain);range("manadrainmindam","manadrainmaxdam",data.manaDrain);
    range("stamdrainmindam","stamdrainmaxdam",data.staminaDrain);write("stunlength",data.stunLength);
    range("burningmin","burningmax",data.burn);write("firelength",data.burnLength);
    write("item_demondamage_percent",data.demonPercent);write("item_undeaddamage_percent",data.undeadPercent);
    write("item_damagetargetac",data.targetArmor);write("damagepercent",data.damagePercent);
    if(data.flags&0x100) write("skill_bypass_undead",1);
    if(data.flags&0x200) write("skill_bypass_demons",1);
    if(data.flags&0x400) write("skill_bypass_beasts",1);
    if(data.flags&2) write("item_deadlystrike",1);
    const auto damageVsType=rules.names.at("damage_vs_montype");size_t count{};
    for(const auto &[key,value]:owner.values) if(key.id==damageVsType && count++<128) writes.emplace_back(key,value);
    return writes;
}
namespace {
void physicalBonuses(MissileHitDamage &hit,const MissileDamageData &data,MissileDamageTarget target,bool monsterTypes) {
    if(target.present) {
        auto percent=data.damagePercent;
        if(target.demon) percent=nativeAdd(percent,data.demonPercent);
        if(target.undead) percent=nativeAdd(percent,data.undeadPercent);
        if(monsterTypes) percent=nativeAdd(percent,target.monsterTypePercent);
        percent=std::max(-90,percent);
        hit.channels[0]=nativeAdd(hit.channels[0],nativeMultiplyDivide(hit.channels[0],percent,100));
    }
    if(data.flags&2) {hit.critical=true;hit.channels[0]=nativeMultiply(hit.channels[0],2);}
}
int32_t poisonLength(const MissileDamageData &data) {
    return data.poisonCount>1?nativeDivide(data.poisonLength,data.poisonCount):data.poisonLength;
}
}
MissileHitDamage rollMissileDamageData(const MissileDamageData &data,const MissileDamageMastery &mastery,MissileDamageTarget target,uint64_t &random) {
    MissileHitDamage hit;
    // Preserve FillDamageParams' draw order; empty channels draw nothing.
    hit.channels[0]=rollMissileDamageValue(data.physical,0,random);
    hit.channels[2]=rollMissileDamageValue(data.fire,mastery.fire,random);
    hit.channels[1]=rollMissileDamageValue(data.magic,mastery.magic,random);
    hit.channels[3]=rollMissileDamageValue(data.lightning,mastery.lightning,random);
    hit.channels[4]=rollMissileDamageValue(data.cold,mastery.cold,random);hit.coldLength=data.coldLength;
    hit.channels[5]=rollMissileDamageValue(data.poison,mastery.poison,random);hit.poisonLength=poisonLength(data);
    hit.manaLeech=data.manaDrain.minimum;hit.lifeLeech=data.lifeDrain.minimum;hit.staminaLeech=data.staminaDrain.minimum;
    hit.burn=rollMissileDamageValue(data.burn,mastery.fire,random);hit.burnLength=data.burnLength;hit.stunLength=data.stunLength;
    physicalBonuses(hit,data,target,true);
    hit.bypassUndead=(data.flags&0x100)!=0;hit.bypassDemon=(data.flags&0x200)!=0;hit.bypassBeasts=(data.flags&0x400)!=0;
    return hit;
}
MissileElementDamage rollMissileElementDamage(const MissileDamageData &data,MissileDamageElement element,const MissileDamageMastery &mastery,
    MissileDamageTarget target,uint64_t &random) {
    MissileElementDamage result;auto &hit=result.hit;
    switch(element) {
    case MissileDamageElement::None:
        hit.channels[0]=rollMissileDamageValue(data.physical,0,random);physicalBonuses(hit,data,target,false);result.value=hit.channels[0];break;
    case MissileDamageElement::Fire:result.value=hit.channels[2]=rollMissileDamageValue(data.fire,mastery.fire,random);break;
    case MissileDamageElement::Lightning:result.value=hit.channels[3]=rollMissileDamageValue(data.lightning,mastery.lightning,random);break;
    case MissileDamageElement::Magic:result.value=hit.channels[1]=rollMissileDamageValue(data.magic,mastery.magic,random);break;
    case MissileDamageElement::Cold:case MissileDamageElement::Freeze:
        result.value=hit.channels[4]=rollMissileDamageValue(data.cold,mastery.cold,random);
        if(element==MissileDamageElement::Freeze) hit.freezeLength=data.coldLength;else hit.coldLength=data.coldLength;
        break;
    case MissileDamageElement::Poison:
        hit.channels[5]=rollMissileDamageValue(data.poison,mastery.poison,random);hit.poisonLength=poisonLength(data);
        result.value=nativeMultiply(hit.channels[5],25);break;
    case MissileDamageElement::LifeDrain:hit.lifeLeech=data.lifeDrain.minimum;break;
    case MissileDamageElement::ManaDrain:hit.manaLeech=data.manaDrain.minimum;break;
    case MissileDamageElement::StaminaDrain:hit.staminaLeech=data.staminaDrain.minimum;break;
    case MissileDamageElement::Stun:hit.stunLength=data.stunLength;break;
    case MissileDamageElement::Burn:
        hit.burn=rollMissileDamageValue(data.burn,mastery.fire,random);hit.burnLength=data.burnLength;
        result.value=nativeMultiply(hit.burn,25);break;
    case MissileDamageElement::Random:break;
    }
    return result;
}

}
