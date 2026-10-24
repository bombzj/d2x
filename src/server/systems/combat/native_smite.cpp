#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace d2x::server::combat {
DomainStatus System::prepareNativeSmite(SpellImpact &impact,const PlayerState &player,TickContext tick) {
    auto &execution=*impact.nativeSkill;if(execution.rolled) return DomainStatus::Applied;
    const auto &gear=player.totals.equipment;
    const bool splash=execution.directFunction==163;
    if(!gear.shield || (!splash && !nativeMeleeReach(impact,0))) return DomainStatus::Unavailable;
    const auto &rules=*player.rules.skills;const auto &catalog=*rules.formulas;
    const auto hand=player.totals.handStats.find(impact.weapon->weapon.item);
    const auto &stats=hand==player.totals.handStats.end()?player.totals.nativeStats:hand->second;
    const auto stat=[&](std::string_view name){return stats.get({rules.stats->names.at(std::string(name)),0});};
    const auto expected=player.combatRandom;auto random=expected;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    auto input=skills::formulaInput(player,draw);SkillFormulaValues values(catalog,input);
    const auto calculation=[&](std::string_view field) {
        const auto value=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,field);
        if(!value) throw std::logic_error("Missing native Smite formula");
        return *value;
    };
    auto low=nativeShiftLeft(gear.smiteMinimum,8),high=nativeShiftLeft(gear.smiteMaximum,8);
    if(const auto *states=ports_.effects.unitEffects(player.actor);states && states->hasState(rules.holyShield.id,tick.tick)) {
        for(const auto &entry:states->entries()) if(entry.spec.state.id==rules.holyShield.id) {
            const auto range=values.physicalRange(entry.spec.source.definition,entry.spec.source.level,true);
            if(!range) throw std::logic_error("Missing posted Holy Shield damage source");
            low=nativeAdd(low,range->first);high=nativeAdd(high,range->second);break;
        }
    }
    // ApplyDamageBonuses(bGetStats=0, shield): item normal damage and the
    // shield's own attribute/mastery bonuses, without critical or weapon ED.
    const auto flat=nativeShiftLeft(stat("item_normaldamage"),8);
    low=nativeAdd(low,flat);high=nativeAdd(high,flat);
    if(low<1) low=256;
    if(high<=low) high=nativeAdd(low,256);
    int32_t mastery{};unsigned count{};
    const auto masteryStat=rules.stats->names.at("passive_mastery_melee_dmg");
    for(const auto &[key,value]:stats.values) if(key.id==masteryStat) {
        if(count++==32) break;
        const auto type=catalog.itemTypes.find(key.layer);
        if(type!=catalog.itemTypes.end() && std::find(gear.shieldTypes.begin(),gear.shieldTypes.end(),type->second)!=gear.shieldTypes.end())
            mastery=std::max(mastery,value);
    }
    auto percent=nativeAdd(calculation("calc1"),stat("damagepercent"));
    percent=nativeAdd(percent,nativeMultiplyDivide(gear.shieldStrengthBonus,stat("strength"),100));
    percent=nativeAdd(percent,nativeMultiplyDivide(gear.shieldDexterityBonus,stat("dexterity"),100));
    percent=std::max(-90,nativeAdd(percent,mastery));
    low=nativeAdd(low,nativePercentage(low,nativeAdd(percent,stat("item_mindamage_percent")),100));
    high=nativeAdd(high,nativePercentage(high,nativeAdd(percent,stat("item_maxdamage_percent")),100));
    MissileHitDamage damage;damage.channels[0]=std::max(0,nativeAdd(low,high>low?int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))):0));
    const auto stun=splash?0:calculation("calc2");const auto &row=catalog.skills.at(execution.skill);
    // Do150 evaluates conversion, but HitFlags 2 skips the Fill stage that
    // would apply it. Do not turn this into generic weapon conversion.
    if(!splash && row.elementType) (void)calculation("calc4");
    rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
    if(splash) {
        const auto range=values.physicalRange(execution.skill,execution.rank,false);
        if(!range) throw std::logic_error("Missing PD2 Smite splash physical damage");
        damage.channels[0]=nativeAdd(damage.channels[0],nativeAdd(range->first,
            int32_t(limitedRandom(random,uint32_t(nativeSubtract(range->second,range->first))))));
    }
    damage.stunLength=stun;damage.hitClass=0x65;damage.knockback=(row.number("resultflags")&8)!=0;
    const auto consumed=ports_.transactions.consumeRandom({player.player,player.actor,player.area,
        ports_.areas.at(player.area).generation,0,tick.tick},expected,random);
    if(!consumed) return consumed.status;
    auto &weapon=*impact.weapon;weapon.smite=weapon.nativeFixedPhysical=true;weapon.critical=false;
    weapon.lifeLeech=weapon.manaLeech=weapon.staminaLeech=0;
    weapon.physicalFlat=weapon.conversionPercent=0;weapon.hitClass=0x65;
    for(size_t channel=0;channel<damage.channels.size();++channel) weapon.channels[channel]=damage.channels[channel];
    weapon.stunFrames=damage.stunLength;weapon.coldFrames=damage.coldLength;weapon.poisonFrames=damage.poisonLength;
    impact.areaDamage->hitFlags=uint32_t(row.number("hitflags"))|2;
    impact.areaDamage->resultFlags|=uint16_t(row.number("resultflags"));
    impact.areaDamage->rolled->value=damage;
    execution.rolled=execution.targetArmorPrepared=execution.fillEventComplete=execution.elementsComplete=true;
    return DomainStatus::Applied;
}
}
