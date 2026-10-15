#include "native_damage.hpp"
#include "formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>
#include <algorithm>

namespace d2x {
void fillNativeWeaponElements(MissileHitDamage &hit,const StatSnapshot &stats,const StatRuleCatalog &rules,int sourceDamage,uint64_t &random,bool nonHirelingMonster) {
    if(!sourceDamage) sourceDamage=128;
    const auto stat=[&](std::string_view name){return stats.get({rules.names.at(std::string(name)),0});};
    const auto range=[&](int32_t low,int32_t high,int32_t mastery,int32_t initial) {
        if(high>0) {
            low=nativeAdd(low,nativePercentage(low,mastery,100));high=nativeAdd(high,nativePercentage(high,mastery,100));
            initial=nativeAdd(initial,low);
            if(high>low) initial=nativeAdd(initial,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))));
        }
        return std::max(initial,0);
    };
    constexpr std::array<std::string_view,4> minimum{"firemindam","lightmindam","coldmindam","magicmindam"},
        maximum{"firemaxdam","lightmaxdam","coldmaxdam","magicmaxdam"},
        masteries{"passive_fire_mastery","passive_ltng_mastery","passive_cold_mastery","passive_mag_mastery"};
    constexpr std::array<size_t,4> channels{2,3,4,1};
    for(size_t slot=0;slot<channels.size();++slot) {
        auto &damage=hit.channels[channels[slot]];const auto high=nativeShiftLeft(stat(maximum[slot]),8);
        damage=high>=8?range(nativeShiftLeft(stat(minimum[slot]),8),high,stat(masteries[slot]),damage):std::max(damage,0);
        if(sourceDamage!=128) damage=nativeMultiplyDivide(damage,sourceDamage,128);
    }
    if(nonHirelingMonster) {
        const auto drain=[&](int32_t &damage,std::string_view minimum,std::string_view maximum) {
            const auto high=nativeShiftLeft(stat(maximum),8);
            damage=high>=8?range(nativeShiftLeft(stat(minimum),8),high,0,damage):std::max(damage,0);
            damage=nativeAdd(damage,nativeMultiplyDivide(damage,sourceDamage,128));
        };
        drain(hit.lifeLeech,"lifedrainmindam","lifedrainmaxdam");drain(hit.manaLeech,"manadrainmindam","manadrainmaxdam");
        drain(hit.staminaLeech,"stamdrainmindam","stamdrainmaxdam");
    } else {
        hit.lifeLeech=nativeAdd(hit.lifeLeech,stat("lifedrainmindam"));hit.manaLeech=nativeAdd(hit.manaLeech,stat("manadrainmindam"));
        hit.bypassUndead|=stat("skill_bypass_undead")!=0;hit.bypassDemon|=stat("skill_bypass_demons")!=0;hit.bypassBeasts|=stat("skill_bypass_beasts")!=0;
    }
    hit.channels[5]=range(stat("poisonmindam"),stat("poisonmaxdam"),stat("passive_pois_mastery"),hit.channels[5]);
    hit.channels[5]=nativeMultiplyDivide(hit.channels[5],sourceDamage,128);
    if(hit.channels[5]) {
        const auto override=stat("skill_poison_override_length");
        if(override>0) hit.poisonLength=nativeAdd(hit.poisonLength,override);
        else {
            hit.poisonLength=nativeAdd(hit.poisonLength,stat("poisonlength"));const auto count=stat("poison_count");
            if(count>1) hit.poisonLength=nativeDivide(hit.poisonLength,count);
        }
    }
    if(hit.channels[4]>0) hit.coldLength=nativeAdd(hit.coldLength,nativeMultiplyDivide(stat("coldlength"),sourceDamage,128));
    if(!hit.stunLength) hit.stunLength=nativeMultiplyDivide(stat("stunlength"),sourceDamage,128);
}
void addNativeSkillElement(MissileHitDamage &hit,MissileDamageElement element,int32_t damage,int32_t length,uint64_t &random) {
    if(element==MissileDamageElement::Random) {
        constexpr MissileDamageElement types[]{MissileDamageElement::Fire,MissileDamageElement::Lightning,
            MissileDamageElement::Cold,MissileDamageElement::Poison};
        element=types[rollRandom(random)&3];if(length<=0) length=50;
    }
    const auto add=[&](size_t channel){hit.channels[channel]=nativeAdd(hit.channels[channel],damage);};
    switch(element) {
    case MissileDamageElement::Fire:add(2);hit.hitClass=0x20;break;
    case MissileDamageElement::Lightning:add(3);hit.hitClass=0x40;break;
    case MissileDamageElement::Magic:add(1);break;
    case MissileDamageElement::Cold:add(4);hit.coldLength=length;hit.hitClass=0x30;break;
    case MissileDamageElement::Poison:add(5);hit.poisonLength=length;hit.hitClass=0x50;break;
    case MissileDamageElement::LifeDrain:hit.lifeLeech=nativeAdd(hit.lifeLeech,damage);break;
    case MissileDamageElement::ManaDrain:hit.manaLeech=nativeAdd(hit.manaLeech,damage);break;
    case MissileDamageElement::StaminaDrain:hit.staminaLeech=nativeAdd(hit.staminaLeech,damage);break;
    case MissileDamageElement::Stun:hit.stunLength=nativeAdd(hit.stunLength,nativeAdd(length,damage));hit.hitClass=0x60;break;
    case MissileDamageElement::Burn:hit.burn=nativeAdd(hit.burn,damage);hit.burnLength=length;break;
    case MissileDamageElement::Freeze:add(4);hit.freezeLength=length;hit.hitClass=0x30;break;
    default:add(0);break;
    }
}
int32_t rollNativeSkillElement(MissileHitDamage &hit,SkillFormulaValues &formulas,int skill,int rank,uint64_t &random) {
    const auto low=formulas.parameter(FormulaDomain::Skill,skill,rank,"enms"),high=formulas.parameter(FormulaDomain::Skill,skill,rank,"exms");
    const auto length=formulas.parameter(FormulaDomain::Skill,skill,rank,"edln");
    if(!low || !high || !length) throw std::logic_error("Unavailable native skill elemental damage");
    const auto damage=nativeAdd(*low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(*high,*low)))));
    // Element identity is prepared from the current Skills row, never from UI text.
    const auto code=formulas.elementalCode(skill);
    MissileDamageElement element=MissileDamageElement::None;
    constexpr std::pair<std::string_view,MissileDamageElement> elements[]{
        {"fire",MissileDamageElement::Fire},{"ltng",MissileDamageElement::Lightning},{"mag",MissileDamageElement::Magic},
        {"cold",MissileDamageElement::Cold},{"pois",MissileDamageElement::Poison},{"life",MissileDamageElement::LifeDrain},
        {"mana",MissileDamageElement::ManaDrain},{"stam",MissileDamageElement::StaminaDrain},{"stun",MissileDamageElement::Stun},
        {"rand",MissileDamageElement::Random},{"burn",MissileDamageElement::Burn},{"frze",MissileDamageElement::Freeze}};
    for(const auto &[name,type]:elements) if(code==name) {element=type;break;}
    addNativeSkillElement(hit,element,damage,*length,random);return damage;
}
MissileHitDamage rollNativeSkillDamage(SkillFormulaValues &formulas,int skill,int rank,uint64_t &random) {
    const auto physical=formulas.physicalRange(skill,rank,false);
    if(!physical) throw std::logic_error("Unavailable native skill physical damage");
    MissileHitDamage hit;hit.channels[0]=nativeAdd(physical->first,int32_t(limitedRandom(random,uint32_t(nativeSubtract(physical->second,physical->first)))));
    rollNativeSkillElement(hit,formulas,skill,rank,random);return hit;
}
}
