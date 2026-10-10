#include "weapon_damage.hpp"
#include "bow_spec.hpp"
#include "spear_spec.hpp"
#include "behavior.hpp"
#include "core/random.hpp"
#include <algorithm>
namespace d2x {
std::array<int64_t,6> targetWeaponChannels(const WeaponSkillDamage &attack,bool demon,bool undead) {
    auto channels=attack.channels;
    const int bonus=(demon?attack.weapon.target.demonDamage:0)+(undead?attack.weapon.target.undeadDamage+(attack.weapon.blunt?50:0):0);
    const auto percent=std::max(-90,attack.physicalPercent+bonus);
    const auto baseLow=attack.physicalMinimum*attack.sourceDamage/128+attack.physicalAddition;
    const auto baseHigh=attack.physicalMaximum*attack.sourceDamage/128+attack.physicalAddition;
    // MissMode::FillDamageParams rolls stored projectile min/max first, then
    // applies target ED. SUnitDmg melee modifies the min/max range before roll.
    const auto low=std::clamp<int64_t>(baseLow+baseLow*(percent+(attack.projectile?0:attack.weapon.minimumDamagePercent))/100,0,INT32_MAX);
    const auto high=std::clamp<int64_t>(baseHigh+baseHigh*(percent+(attack.projectile?0:attack.weapon.maximumDamagePercent))/100,low,INT32_MAX);
    if(attack.projectile) {
        const auto raw=baseLow+(baseHigh>baseLow?attack.physicalRoll%uint32_t(baseHigh-baseLow):0);
        channels[0]=(raw+raw*percent/100)*(attack.critical?2:1);
    } else channels[0]=(low+(high>low?attack.physicalRoll%uint32_t(high-low):0))*(attack.critical?2:1);
    channels[0]+=int64_t(attack.physicalFlat)*256;
    const auto converted=channels[0]*attack.conversionPercent/100;
    channels[0]-=converted;channels[size_t(attack.conversionElement)]+=converted;return channels;
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
    const auto &mods=attributes.combat;
    const auto own=mods.weapons.find(weapon.item);
    const int flat=mods.normalDamage+(own==mods.weapons.end()?0:own->second.normalDamage);
    value.physicalMinimum=int64_t(equipment.smiteMinimum+mods.smiteMinimum+flat)*256;
    value.physicalMaximum=int64_t(equipment.smiteMaximum+mods.smiteMaximum+flat)*256;
    value.physicalPercent=attributes.strength+mods.damagePercent+skill.weapon->damagePercent;
    value.physicalRoll=rollRandom(random);value.stunFrames=skill.weapon->stunFrames;
    if(weapon.item) {value.wearChance=4;value.wearAmount=1;value.wearSkill=skill;}
    value.channels[0]=targetWeaponChannels(value,false,false)[0];return value;
}
WeaponSkillDamage rollWeaponSkillDamage(const WeaponDamage &weapon,const CombatModifiers &mods,
    const SkillCastSpec &skill,int level,bool projectile,uint64_t &random) {
    WeaponSkillDamage value; value.weapon=weapon;value.level=level;
    value.selfDamagePercent=skill.weapon->selfDamagePercent;
    if(!projectile && weapon.item) {value.wearChance=4;value.wearAmount=1;value.wearSkill=skill;}
    value.weapon.attackRatingPercent+=skill.weapon->attackRating;
    value.physicalPercent=(projectile?weapon.projectileDamagePercent:weapon.damagePercent)+skill.weapon->damagePercent;
    const auto roll=[&](int64_t low,int64_t high) {
        low=std::clamp<int64_t>(low,0,INT32_MAX);high=std::clamp<int64_t>(high,low,INT32_MAX);
        return low+limitedRandom(random,uint32_t(high-low));
    };
    value.projectile=projectile;value.physicalMinimum=projectile?weapon.projectileMinimum:weapon.meleeBaseMinimum;
    value.physicalMaximum=projectile?weapon.projectileMaximum:weapon.meleeBaseMaximum;
    value.physicalRoll=rollRandom(random);
    auto own=mods.weapons.find(weapon.item);const WeaponModifiers extra=own==mods.weapons.end()?WeaponModifiers{}:own->second;
    value.lifeLeech=std::max(0,mods.lifeLeech+extra.lifeLeech);
    const int crushing=std::clamp(mods.crushingBlow+extra.crushingBlow,0,100),wounds=std::clamp(mods.openWounds+extra.openWounds,0,100);
    value.crushing=crushing && limitedRandom(random,100)<unsigned(crushing);
    value.openWounds=wounds && limitedRandom(random,100)<unsigned(wounds);
    value.physicalFlat=skill.weapon->physicalFlat;value.stunFrames=skill.weapon->stunFrames;value.knockback=skill.weapon->knockback;
    const auto ranges=attackElementRanges(mods,weapon.item);
    value.channels[1]=roll(int64_t(ranges.magic.minimum)*256,int64_t(ranges.magic.maximum)*256);
    value.channels[2]=roll(int64_t(ranges.fire.minimum)*256,int64_t(ranges.fire.maximum)*256);
    value.channels[3]=roll(int64_t(ranges.lightning.minimum)*256,int64_t(ranges.lightning.maximum)*256);
    value.channels[4]=roll(int64_t(ranges.cold.minimum)*256,int64_t(ranges.cold.maximum)*256);
    value.channels[5]=roll(mods.poisonMinimum+extra.poisonMinimum,mods.poisonMaximum+extra.poisonMaximum);
    value.coldFrames=mods.coldFrames+extra.coldFrames;
    if(skill.effect==SkillBehavior::Vengeance) {
        const auto flat=int64_t(mods.normalDamage+extra.normalDamage)*256;
        const auto base=roll(value.physicalMinimum-flat,value.physicalMaximum-flat);
        constexpr size_t channels[]{2,4,3};
        for(size_t i=0;i<3;++i) value.channels[channels[i]]+=base*skill.weapon->elementPercent[i]/100;
        value.coldFrames+=int(skill.coldDuration*25.f+.001f);
    }
    value.poisonFrames=(mods.poisonFrames+extra.poisonFrames)/std::max(1,mods.poisonSources+extra.poisonSources);
    value.pierceChance=mods.pierce;
    bool critical=limitedRandom(random,100)<unsigned(std::clamp(mods.criticalStrike,0,100));
    if(!critical) critical=limitedRandom(random,100)<unsigned(std::clamp(mods.deadlyStrike+extra.deadlyStrike,0,100));
    value.critical=critical;
    const auto elemental=roll(int64_t(skill.minimumDamage*256.f),int64_t(skill.maximumDamage*256.f));
    if(skill.poisonDuration>0 && !skill.weapon->spear) {
        value.channels[5]+=elemental;value.poisonFrames=int(skill.poisonDuration*25.f+.001f);
    }
    if(skill.weapon->bow) {
        const auto &bow=*skill.weapon->bow;
        value.sourceDamage=bow.sourceDamage;
        for(size_t i=1;i<value.channels.size();++i) value.channels[i]=value.channels[i]*bow.sourceDamage/128;
        value.coldFrames=value.coldFrames*bow.sourceDamage/128;
        if(bow.physicalSkillDamage) value.physicalAddition=elemental;
        else if(bow.element==DamageType::Fire || bow.element==DamageType::Cold) value.channels[size_t(bow.element)]+=elemental;
        if(bow.element==DamageType::Cold) value.coldFrames+=int(skill.coldDuration*25.f+.001f);
        value.conversionPercent=bow.conversionPercent;value.conversionElement=bow.element;
        // SrvDmg02: convert total cold length (including equipment) to freeze
        // length. Target rank, resistance and difficulty belong to combat.
        value.freeze=bow.freezePercent>0; if(value.freeze) value.coldFrames=value.coldFrames*bow.freezePercent/100;
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
