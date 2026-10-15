#include "weapon_damage.hpp"
#include "bow_spec.hpp"
#include "spear_spec.hpp"
#include "behavior.hpp"
#include "core/random.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>
namespace d2x {
namespace {
std::pair<int32_t,int32_t> meleePhysicalRange(const WeaponSkillDamage &attack,bool demon,bool undead) {
    auto low=nativeChecked(attack.physicalMinimum),high=nativeChecked(attack.physicalMaximum);
    if(low<1) low=256;
    if(high<=low) high=nativeAdd(low,256);
    const int demonBonus=demon?std::max(0,attack.weapon.target.demonDamage):0;
    const int undeadBonus=undead?std::max(0,nativeAdd(attack.weapon.target.undeadDamage,attack.weapon.blunt?50:0)):0;
    const auto percent=std::max(-90,nativeAdd(attack.physicalPercent,nativeAdd(demonBonus,undeadBonus)));
    return {nativeAdd(low,nativePercentage(low,nativeAdd(percent,attack.weapon.minimumDamagePercent),100)),
        nativeAdd(high,nativePercentage(high,nativeAdd(percent,attack.weapon.maximumDamagePercent),100))};
}
bool weaponCritical(const WeaponDamage &weapon,const CombatModifiers &mods,const SkillCastSpec &skill,bool projectile,uint64_t &random) {
    const auto own=mods.weapons.find(weapon.item);const WeaponModifiers extra=own==mods.weapons.end()?WeaponModifiers{}:own->second;
    const int mastery=weapon.nativeModifiers?(weapon.throwable && skill.weapon->useThrowMastery?weapon.throwMastery[2]:weapon.meleeMastery[2]):
        projectile?weapon.projectileMasteryCritical:weapon.masteryCritical;
    bool critical=mastery>0 && limitedRandom(random,100)<unsigned(std::clamp(mastery,0,100));
    if(!critical && mods.criticalStrike>0) critical=limitedRandom(random,100)<unsigned(mods.criticalStrike);
    const auto deadly=nativeAdd(mods.deadlyStrike,extra.deadlyStrike);
    if(!critical && deadly>0) critical=limitedRandom(random,100)<unsigned(deadly);
    return critical;
}
}
std::array<int64_t,6> targetWeaponChannels(const WeaponSkillDamage &attack,bool demon,bool undead) {
    auto channels=attack.channels;
    const int demonBonus=demon?attack.weapon.target.demonDamage:0;
    const int undeadBonus=undead?nativeAdd(attack.weapon.target.undeadDamage,attack.weapon.blunt?50:0):0;
    const int bonus=nativeAdd(attack.projectile?demonBonus:std::max(0,demonBonus),attack.projectile?undeadBonus:std::max(0,undeadBonus));
    const auto percent=std::max(-90,nativeAdd(attack.physicalPercent,bonus));
    auto baseLow=nativeChecked(attack.physicalMinimum),baseHigh=nativeChecked(attack.physicalMaximum);
    if(attack.projectile) {
        baseLow=nativeAdd(nativeMultiplyDivide(baseLow,attack.sourceDamage,128),nativeChecked(attack.physicalAddition));
        baseHigh=nativeAdd(nativeMultiplyDivide(baseHigh,attack.sourceDamage,128),nativeChecked(attack.physicalAddition));
    } else {
        if(baseLow<1) baseLow=256;
        if(baseHigh<=baseLow) baseHigh=nativeAdd(baseLow,256);
    }
    // MissMode::FillDamageParams rolls stored projectile min/max first, then
    // applies target ED. SUnitDmg melee modifies the min/max range before roll.
    if(attack.nativeFixedPhysical) {
        channels[0]=attack.channels[0];
    } else if(attack.projectile) {
        const auto raw=nativeAdd(baseLow,baseHigh>baseLow?int32_t(attack.physicalRoll%uint32_t(nativeSubtract(baseHigh,baseLow))):0);
        channels[0]=nativeMultiply(nativeAdd(raw,nativeMultiplyDivide(raw,percent,100)),attack.critical?2:1);
    } else {
        const auto [low,high]=meleePhysicalRange(attack,demon,undead);
        auto raw=std::max(0,nativeAdd(nativeChecked(attack.physicalAddition),nativeAdd(low,high>low?int32_t(attack.physicalRoll%uint32_t(nativeSubtract(high,low))):0)));
        if(attack.sourceDamage!=128) raw=nativePercentage(raw,attack.sourceDamage,128);
        channels[0]=nativeMultiply(raw,attack.critical?2:1);
    }
    channels[0]=nativeAdd(nativeChecked(channels[0]),nativeShiftLeft(attack.physicalFlat,8));
    const auto converted=nativePercentage(nativeChecked(channels[0]),attack.conversionPercent,100);
    channels[0]=nativeSubtract(nativeChecked(channels[0]),converted);
    channels[size_t(attack.conversionElement)]=nativeAdd(nativeChecked(channels[size_t(attack.conversionElement)]),converted);
    for(const auto &conversion:attack.progressiveConversions) {
        const auto base=nativeChecked(channels[0]);
        const auto part=std::min(nativePercentage(base,conversion.percent,100),base);
        channels[0]=nativeSubtract(base,part);
        channels[size_t(conversion.element)]=nativeAdd(nativeChecked(channels[size_t(conversion.element)]),part);
    }
    return channels;
}
WeaponSkillDamage rollPotionDamage(const WeaponDamage &weapon,int level,uint64_t &random) {
    WeaponSkillDamage value;value.weapon=weapon;value.level=level;value.automatic=true;value.projectile=true;
    // SrcDamage=0: potion table damage excludes weapon ED, critical and equipment elements.
    value.weapon.target={};value.weapon.blunt=false;
    const auto &ranges=weapon.projectile->damage;
    value.physicalMinimum=ranges[0].minimum;value.physicalMaximum=ranges[0].maximum;value.physicalRoll=rollRandom(random);
    for(size_t channel=1;channel<ranges.size();++channel)
        value.channels[channel]=ranges[channel].minimum+limitedRandom(random,uint32_t(std::max(0,ranges[channel].maximum-ranges[channel].minimum)));
    value.channels[0]=targetWeaponChannels(value,false,false)[0];return value;
}
WeaponSkillDamage rollSmiteDamage(const WeaponDamage &weapon,const EquipmentStats &equipment,const CharacterAttributes &attributes,const SkillCastSpec &skill,int level,uint64_t &random) {
    WeaponSkillDamage value;value.weapon=weapon;value.level=level;value.automatic=value.smite=true;
    value.weapon.target={};value.weapon.blunt=false;value.weapon.minimumDamagePercent=value.weapon.maximumDamagePercent=0;
    value.weapon.hitClass=101;
    const auto &mods=weapon.nativeModifiers?*weapon.nativeModifiers:attributes.combat;
    const auto own=mods.weapons.find(weapon.item);
    value.crushingBlow=std::clamp(mods.crushingBlow+(own==mods.weapons.end()?0:own->second.crushingBlow),0,100);
    value.openWounds=std::clamp(mods.openWounds+(own==mods.weapons.end()?0:own->second.openWounds),0,100);
    const int flat=mods.normalDamage+(own==mods.weapons.end()?0:own->second.normalDamage);
    value.physicalMinimum=int64_t(equipment.smiteMinimum+mods.smiteMinimum+flat)*256;
    value.physicalMaximum=int64_t(equipment.smiteMaximum+mods.smiteMaximum+flat)*256;
    value.physicalPercent=attributes.strength+mods.damagePercent+skill.weapon->damagePercent;
    value.physicalRoll=rollRandom(random);value.stunFrames=skill.weapon->stunFrames;
    if(weapon.item) {value.wearChance=4;value.wearAmount=1;value.wearSkill=skill;}
    value.channels[0]=targetWeaponChannels(value,false,false)[0];return value;
}
WeaponSkillDamage prepareWeaponSkillProcedures(const WeaponDamage &weapon,const CombatModifiers &input,
    const SkillCastSpec &skill,int level,bool projectile) {
    const auto &mods=weapon.nativeModifiers?*weapon.nativeModifiers:input;
    WeaponSkillDamage value; value.weapon=weapon;value.level=level;
    value.wearSkill=skill;
    value.selfDamagePercent=skill.weapon->selfDamagePercent;
    value.conversionPercent=std::clamp(skill.weapon->magicConversionPercent,0,100);value.conversionElement=DamageType::Magic;
    if(!projectile && weapon.item) {value.wearChance=4;value.wearAmount=1;value.wearSkill=skill;}
    value.weapon.attackRatingPercent+=skill.weapon->attackRating;
    if(weapon.nativeModifiers) {
        const auto selected=weapon.throwable && skill.weapon->useThrowMastery?weapon.throwMastery:weapon.meleeMastery;
        value.weapon.attackRatingPercent=nativeAdd(nativeSubtract(value.weapon.attackRatingPercent,weapon.meleeMastery[0]),selected[0]);
        value.physicalPercent=nativeAdd(nativeAdd(nativeAdd(weapon.statDamagePercent,projectile?weapon.projectileAttributeDamagePercent:weapon.attributeDamagePercent),selected[1]),skill.weapon->damagePercent);
        if(projectile) value.physicalPercent=nativeAdd(value.physicalPercent,std::max(weapon.minimumDamagePercent,weapon.maximumDamagePercent));
    } else {
        if (projectile) value.weapon.attackRatingPercent += weapon.projectileMasteryRatingAdjustment;
        value.physicalPercent=(projectile?weapon.projectileDamagePercent:weapon.damagePercent)+skill.weapon->damagePercent;
    }
    value.projectile=projectile;value.physicalMinimum=projectile?weapon.projectileMinimum:weapon.meleeBaseMinimum;
    value.physicalMaximum=projectile?weapon.projectileMaximum:weapon.meleeBaseMaximum;
    const auto own=mods.weapons.find(weapon.item);const WeaponModifiers extra=own==mods.weapons.end()?WeaponModifiers{}:own->second;
    value.lifeLeech=std::max(0,mods.lifeLeech+extra.lifeLeech);
    value.manaLeech=std::max(0,mods.manaLeech+extra.manaLeech);
    value.crushingBlow=std::clamp(mods.crushingBlow+extra.crushingBlow,0,100);
    value.openWounds=std::clamp(mods.openWounds+extra.openWounds,0,100);
    value.physicalFlat=skill.weapon->physicalFlat;value.stunFrames=skill.weapon->stunFrames;value.knockback=skill.weapon->knockback;
    value.clearTargetStun=skill.weapon->clearTargetStun;
    value.coldFrames=mods.coldFrames+extra.coldFrames;
    value.poisonFrames=weaponPoisonLength(int64_t(mods.poisonFrames)+extra.poisonFrames,mods.poisonSources+extra.poisonSources,0,nativeAdd(mods.poisonOverrideFrames,extra.poisonOverrideFrames));
    value.pierceChance=mods.pierce;
    if(skill.weapon->bow) {
        const auto &bow=*skill.weapon->bow;
        value.sourceDamage=bow.sourceDamage;value.conversionPercent=bow.conversionPercent;value.conversionElement=bow.element;
        value.freeze=bow.freezePercent>0;value.automatic=bow.guided;
    }
    if(skill.weapon->spear) {
        const auto &spear=*skill.weapon->spear;value.automatic=spear.automaticHit;
        if(spear.kind==SpearSkillSpec::Kind::Impale) {value.wearChance=spear.wearChance;value.wearAmount=spear.wearAmount;value.wearSkill=skill;}
        if(spear.kind==SpearSkillSpec::Kind::Bolt) {value.conversionPercent=spear.conversionPercent;value.conversionElement=DamageType::Lightning;}
    }
    return value;
}
WeaponSkillDamage rollNativeWeaponPhysical(const WeaponDamage &weapon,const CombatModifiers &input,const SkillCastSpec &skill,int level,bool demon,bool undead,uint64_t &random,NativeWeaponPhysicalInput native) {
    const auto &mods=weapon.nativeModifiers?*weapon.nativeModifiers:input;
    auto value=prepareWeaponSkillProcedures(weapon,input,skill,level,false);const auto [low,high]=meleePhysicalRange(value,demon,undead);
    value.sourceDamage=native.sourceDamage?native.sourceDamage:128;value.physicalAddition=native.initialDamage;
    value.physicalRoll=high>low?limitedRandom(random,uint32_t(nativeSubtract(high,low))):0;
    value.critical=weaponCritical(weapon,mods,skill,false,random);
    value.channels[0]=targetWeaponChannels(value,demon,undead)[0];return value;
}
WeaponSkillDamage rollWeaponSkillDamage(const WeaponDamage &weapon,const CombatModifiers &input,
    const SkillCastSpec &skill,int level,bool projectile,uint64_t &random) {
    const auto &mods=weapon.nativeModifiers?*weapon.nativeModifiers:input;
    auto value=prepareWeaponSkillProcedures(weapon,input,skill,level,projectile);
    const auto roll=[&](int64_t low,int64_t high) {
        low=std::clamp<int64_t>(low,0,INT32_MAX);high=std::clamp<int64_t>(high,low,INT32_MAX);
        return low+limitedRandom(random,uint32_t(high-low));
    };
    value.physicalRoll=rollRandom(random);
    const auto own=mods.weapons.find(weapon.item);const WeaponModifiers extra=own==mods.weapons.end()?WeaponModifiers{}:own->second;
    const auto ranges=attackElementRanges(mods,weapon.item);
    const auto elementalRange=[&](AttackDamageRange range,int mastery) {
        const auto lower=nativeShiftLeft(range.minimum,8),upper=nativeShiftLeft(range.maximum,8);
        return roll(nativeAdd(lower,nativePercentage(lower,mastery,100)),nativeAdd(upper,nativePercentage(upper,mastery,100)));
    };
    value.channels[2]=elementalRange(ranges.fire,mods.fireSkillDamagePercent);
    value.channels[3]=elementalRange(ranges.lightning,mods.lightningSkillDamagePercent);
    value.channels[4]=elementalRange(ranges.cold,mods.coldSkillDamagePercent);
    value.channels[1]=elementalRange(ranges.magic,mods.magicSkillDamagePercent);
    const auto poisonMastery=[&](int64_t rate) {return poisonMasteryRate(rate,mods.poisonSkillDamagePercent);};
    value.channels[5]=roll(poisonMastery(int64_t(mods.poisonMinimum)+extra.poisonMinimum),poisonMastery(int64_t(mods.poisonMaximum)+extra.poisonMaximum));
    value.coldFrames=mods.coldFrames+extra.coldFrames;
    if(skill.effect==SkillBehavior::Vengeance) {
        const auto flat=int64_t(mods.normalDamage+extra.normalDamage)*256;
        const auto base=roll(value.physicalMinimum-flat,value.physicalMaximum-flat);
        constexpr size_t channels[]{2,4,3};
        for(size_t i=0;i<3;++i) value.channels[channels[i]]=nativeAdd(nativeChecked(value.channels[channels[i]]),nativePercentage(nativeChecked(base),skill.weapon->elementPercent[i],100));
        value.coldFrames+=int(skill.coldDuration*25.f+.001f);
    }
    value.poisonFrames=weaponPoisonLength(int64_t(mods.poisonFrames)+extra.poisonFrames,mods.poisonSources+extra.poisonSources,0,nativeAdd(mods.poisonOverrideFrames,extra.poisonOverrideFrames));
    value.pierceChance=mods.pierce;
    value.critical=weaponCritical(weapon,mods,skill,projectile,random);
    const auto elemental=roll(skill.minimumDamage.raw(),skill.maximumDamage.raw());
    if(skill.poisonDuration>0 && !skill.weapon->spear) {
        value.channels[5]+=elemental;
        // SUnitDmg divides the combined skill/item length by item poison count;
        // the skill itself does not add an equipment source.
        value.poisonFrames=weaponPoisonLength(int64_t(mods.poisonFrames)+extra.poisonFrames,mods.poisonSources+extra.poisonSources,int(skill.poisonDuration*25.f+.001f),nativeAdd(mods.poisonOverrideFrames,extra.poisonOverrideFrames));
    }
    if(skill.weapon->bow) {
        const auto &bow=*skill.weapon->bow;
        value.sourceDamage=bow.sourceDamage;
        for(size_t i=1;i<value.channels.size();++i) value.channels[i]=nativeMultiplyDivide(nativeChecked(value.channels[i]),bow.sourceDamage,128);
        value.coldFrames=nativeMultiplyDivide(value.coldFrames,bow.sourceDamage,128);
        if(bow.physicalSkillDamage) value.physicalAddition=elemental;
        else if(bow.element==DamageType::Fire || bow.element==DamageType::Cold) value.channels[size_t(bow.element)]+=elemental;
        if(bow.element==DamageType::Cold) value.coldFrames+=int(skill.coldDuration*25.f+.001f);
        value.conversionPercent=bow.conversionPercent;value.conversionElement=bow.element;
        // SrvDmg02: convert total cold length (including equipment) to freeze
        // length. Target rank, resistance and difficulty belong to combat.
        value.freeze=bow.freezePercent>0; if(value.freeze) value.coldFrames=nativePercentage(value.coldFrames,bow.freezePercent,100);
        value.automatic=bow.guided;
    }
    if(skill.weapon->spear) {
        const auto &spear=*skill.weapon->spear;
        value.automatic=spear.automaticHit;
        if(spear.kind==SpearSkillSpec::Kind::Impale) {
            value.wearChance=spear.wearChance;value.wearAmount=spear.wearAmount;value.wearSkill=skill;
        }
        if(spear.kind==SpearSkillSpec::Kind::Bolt) {
            // SrvDmg12 keeps lightning, converts the enhanced physical channel,
            // and clears other channels. Target bonuses are applied at contact.
            value.channels[1]=value.channels[2]=value.channels[4]=value.channels[5]=0;
            value.channels[3]+=elemental;value.coldFrames=value.poisonFrames=0;
            value.conversionPercent=spear.conversionPercent;value.conversionElement=DamageType::Lightning;
        } else if(spear.kind==SpearSkillSpec::Kind::Power || spear.kind==SpearSkillSpec::Kind::Charged ||
            spear.kind==SpearSkillSpec::Kind::Strike || spear.kind==SpearSkillSpec::Kind::Fury) value.channels[3]+=elemental;
        else if(spear.poisonTrail || skill.poisonDuration>0) {value.channels[5]=elemental;value.poisonFrames=int(skill.poisonDuration*25.f+.001f);}
    }
    value.channels[0]=targetWeaponChannels(value,false,false)[0];
    return value;
}
}
