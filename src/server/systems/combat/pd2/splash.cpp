#include "../system.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "../player_contact.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/combat/weapon_block.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <stdexcept>

namespace d2x::server::combat {
DomainStatus System::preparePd2SplashDamage(SpellImpact &impact,ParticipantView source,TickContext tick) {
    auto &execution=*impact.nativeSkill;
    if(execution.rolled || execution.programPrefilled) return DomainStatus::Applied;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const bool copied=execution.skill==255 || execution.skill==270 || execution.skill==275;
    if(copied && !execution.initialDamage) return DomainStatus::Unavailable;
    auto damage=execution.initialDamage?*execution.initialDamage:MissileHitDamage{};
    const auto expected=caster->random();auto random=expected;
    const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
        return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
    };
    auto input=caster->formulaInput(ports_.effects,tick.tick,draw);
    SkillFormulaValues values(*caster->programs()->formulas,input);
    // Callback 0x303360 adds skill elements and physical damage after copying
    // the kick's allocated damage. HitFlags 2 then skips the weapon Fill.
    rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
    const auto physical=values.physicalRange(execution.skill,execution.rank,false);
    if(!physical) throw std::logic_error("Missing PD2 splash physical damage");
    damage.channels[0]=nativeAdd(damage.channels[0],nativeAdd(physical->first,
        int32_t(limitedRandom(random,uint32_t(nativeSubtract(physical->second,physical->first))))));
    const auto consumed=caster->player?ports_.transactions.consumeRandom(caster->context(ports_.areas,tick.tick),expected,random):
        ports_.monsters.consumeRandom(source.id(),expected,random);
    if(!consumed) return consumed.status;
    execution.programPrefilled=true;
    execution.initialDamage=std::make_shared<MissileHitDamage>(damage);
    if(copied) {
        if(damage.poisonLength) damage.poisonLength=std::max(1,int32_t(uint32_t(damage.poisonLength)/2u));
        impact.areaDamage->rolled->value=damage;
        auto &weapon=*impact.weapon;
        for(size_t channel=0;channel<damage.channels.size();++channel) weapon.channels[channel]=damage.channels[channel];
        weapon.nativeFixedPhysical=true;weapon.physicalFlat=weapon.conversionPercent=0;
        weapon.lifeLeech=damage.lifeLeech;weapon.manaLeech=damage.manaLeech;weapon.staminaLeech=damage.staminaLeech;
        weapon.coldFrames=damage.coldLength;weapon.freezeFrames=damage.freezeLength;weapon.poisonFrames=damage.poisonLength;
        weapon.stunFrames=damage.stunLength;weapon.knockback=damage.knockback;weapon.hitClass=damage.hitClass;
        execution.rolled=execution.targetArmorPrepared=execution.fillEventComplete=execution.elementsComplete=true;
    } else impact.areaDamage->hitFlags&=~uint32_t{2};
    return DomainStatus::Applied;
}
DomainStatus System::preparePd2SplashDefense(SpellImpact &impact,ParticipantView source,ParticipantView target,TickContext tick) {
    auto &contact=*impact.missileContact;
    const auto active=state_.pd2AvoidUntil.find(target.id());
    if(target.player && active!=state_.pd2AvoidUntil.end() && active->second>tick.tick) {
        contact.defensePrepared=true;return DomainStatus::Applied;
    }
    const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
    if(!stats) return DomainStatus::Unavailable;
    const auto &rules=target.player?*target.player->rules.skills->stats:*target.monster->rule.stats;
    const auto *gear=target.player?&target.player->totals.equipment:target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
    const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
    const auto region=int(target.player?target.player->area:target.monster->area);
    const bool pvpArea=target.player && (region==157 || region==159 || region==166);
    const auto roll=[&](int chance) {return chance>0 && pd2UnitEventRandom(random)%100<uint32_t(pvpArea?chance/2:chance);};
    NativeBlockOrDodge defense;
    if(gear && (gear->animationClass=="2hs" || gear->animationClass=="ht2")) {
        auto chance=std::min(75,nativeWeaponBlockChance(*gear,*stats,rules));
        if(source.monster && source.monster->rule.nativeClass==1112) chance/=3;
        if(roll(chance)) defense.block=defense.weaponBlock=true;
    }
    // Current 0x3037C0 is a dedicated avoidance finder, not shield block.
    // A failed moving Evade continues to Avoid; Do163 passes missile=1.
    if(!defense.block) {
        if(target.moving() && roll(stats->get({rules.names.at("passive_evade"),0}))) defense.avoidance=WeaponAvoidance::Evade;
        else if(roll(stats->get({rules.names.at("passive_avoid"),0}))) defense.avoidance=WeaponAvoidance::Avoid;
    }
    const auto consumed=target.player?ports_.transactions.consumeRandom({target.player->player,target.id(),target.player->area,
        ports_.areas.at(target.player->area).generation,0,tick.tick},expected,random):ports_.monsters.consumeRandom(target.id(),expected,random);
    if(!consumed) return consumed.status;
    if(target.player && (defense.block || defense.avoidance!=WeaponAvoidance::None)) state_.pd2AvoidUntil[target.id()]=tick.tick+4;
    contact.blocked=defense.block;contact.avoidance=defense.avoidance;contact.defensePrepared=true;
    return DomainStatus::Applied;
}
}
