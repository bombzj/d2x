#include "weapon_damage.hpp"
#include "bow_spec.hpp"
#include "spear_spec.hpp"
#include "behavior.hpp"
#include "core/random.hpp"
#include "gameplay/combat/poison.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2/arithmetic.hpp"
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
Pd2CriticalRoll weaponCritical(const WeaponDamage &weapon,const CombatModifiers &mods,const SkillCastSpec &skill,bool projectile,uint64_t &random) {
    const auto own=mods.weapons.find(weapon.item);const WeaponModifiers extra=own==mods.weapons.end()?WeaponModifiers{}:own->second;
    const int mastery=weapon.nativeModifiers?(weapon.throwable && skill.weapon->useThrowMastery?weapon.throwMastery[2]:weapon.meleeMastery[2]):
        projectile?weapon.projectileMasteryCritical:weapon.masteryCritical;
    if(mods.pd2) return rollPd2Critical(*mods.pd2,mods.criticalStrike,mastery,nativeAdd(mods.deadlyStrike,extra.deadlyStrike),random);
    bool critical=mastery>0 && limitedRandom(random,100)<unsigned(std::clamp(mastery,0,100));
    if(!critical && mods.criticalStrike>0) critical=limitedRandom(random,100)<unsigned(mods.criticalStrike);
    const auto deadly=nativeAdd(mods.deadlyStrike,extra.deadlyStrike);
    if(!critical && deadly>0) critical=limitedRandom(random,100)<unsigned(deadly);
    return {critical?1:0,200};
}
}
std::array<int64_t,6> targetWeaponChannels(const WeaponSkillDamage &attack,bool demon,bool undead) {
    auto channels=attack.channels;
    const auto criticalDamage=[&](int32_t damage) {
        if(!attack.critical) return damage;
        if(attack.pd2CriticalScaling) return pd2MultiplyDivide(damage,attack.criticalPercent,100);
        return attack.criticalPercent==200?nativeMultiply(damage,2):nativeMultiplyDivide(damage,attack.criticalPercent,100);
    };
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
        const auto amount=nativeAdd(raw,nativeMultiplyDivide(raw,percent,100));
        channels[0]=criticalDamage(amount);
    } else {
        const auto [low,high]=meleePhysicalRange(attack,demon,undead);
        auto raw=std::max(0,nativeAdd(nativeChecked(attack.physicalAddition),nativeAdd(low,high>low?int32_t(attack.physicalRoll%uint32_t(nativeSubtract(high,low))):0)));
        if(attack.sourceDamage!=128) raw=nativePercentage(raw,attack.sourceDamage,128);
        channels[0]=criticalDamage(raw);
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
WeaponSkillDamage prepareWeaponSkillProcedures(const WeaponDamage &weapon,const CombatModifiers &input,
    const SkillCastSpec &skill,int level,bool projectile) {
    const auto &mods=weapon.nativeModifiers?*weapon.nativeModifiers:input;
    WeaponSkillDamage value; value.weapon=weapon;value.level=level;
    value.pd2CriticalScaling=mods.pd2.has_value();
    value.wearSkill=skill;
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
    const auto critical=native.critical?weaponCritical(weapon,mods,skill,false,random):Pd2CriticalRoll{};
    value.critical=critical.kind!=0;value.criticalPercent=critical.percentage;
    // FillDamageValues invokes its unit event before applying conversion.
    // Keep the original roll/parameters for the later conversion stage.
    auto physical=value;physical.conversionPercent=0;physical.physicalFlat=0;
    value.channels[0]=targetWeaponChannels(physical,demon,undead)[0];return value;
}
}
