#include "system.hpp"
#include "pd2/wounds.hpp"
#include "gameplay/combat/weapon_block.hpp"
#include "player_contact.hpp"
#include "pd2/crushing.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/attributes/monster_resources.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/runtime/equipment_rules.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/runtime/skill_rules.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/unit_values.hpp"
#include "gameplay/combat/avoidance.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "gameplay/rules/pd2_random.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace d2x::server::combat {
DomainResult<MissilePreludeResult> System::missilePrelude(SpellImpact &impact,TickContext tick) {
    if(!impact.missileDamage || impact.targets.size()!=1 || impact.next) return {DomainStatus::InvalidRequest,{}};
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(impact.source),target=participants.find(impact.targets.front());
    const auto *area=ports_.areas.find(impact.area);
    if(!area || !target.aliveIn(impact.area) ||
        (source.id() && !impact.environment && !impact.collideFriend && !participants.canHarm(source,target))) return {DomainStatus::Applied,MissilePreludeResult::Ignore};
    if(impact.nativeNextHit && !impact.missileContact && !(impact.pd2NextHitKey?
        ports_.effects.pd2MissileHitAllowed(target.id(),*impact.pd2NextHitKey):ports_.effects.missileHitAllowed(target.id(),tick.tick)))
        return {DomainStatus::Applied,MissilePreludeResult::Ignore};
    impact.binding=participants.bind(source);
    const auto result=prepareMissileContact(impact,source,target,area->generation,tick,true);
    if(result!=DomainStatus::Applied) return {result,{}};
    return {DomainStatus::Applied,impact.missileContact->hit?MissilePreludeResult::Hit:MissilePreludeResult::Miss};
}
DomainStatus System::prepareMissileContact(SpellImpact &impact,ParticipantView source,ParticipantView target,
    uint64_t generation,TickContext tick,bool preludeOnly) {
    if(!impact.missileContact || impact.missileContact->targetIndex!=impact.next) {
        impact.missileContact=std::make_shared<MissileContactExecution>();
        impact.missileContact->targetIndex=impact.next;
    }
    auto &contact=*impact.missileContact;
    if(impact.nativeDamageFlagsOnly || impact.areaDamage) {
        contact.accuracyPrepared=contact.hit=contact.delayPrepared=true;
        impact.missileEventTarget=impact.next;
    }
    const auto seed=[](ParticipantView unit) {return unit.player?unit.player->combatRandom:unit.monster->combatRandom;};
    const auto publishSeed=[&](ParticipantView unit,uint64_t expected,uint64_t next) {
        if(unit.monster) return ports_.monsters.consumeRandom(unit.id(),expected,next).status;
        const auto &player=*unit.player;const auto *area=ports_.areas.find(player.area);
        if(!area) return DomainStatus::Stale;
        return ports_.transactions.consumeRandom({player.player,player.actor,player.area,area->generation,0,tick.tick},expected,next).status;
    };
    if(!contact.accuracyPrepared) {
        const auto expected=source.id()?seed(source):0;auto random=expected;
        contact.hit=true;
        if(impact.monsterToHit && !source.id()) contact.hit=false;
        else if(impact.monsterToHit) {
            // IsHitSuccessful draws from the actual owner even against a
            // running player. The melee-only running shortcut does not apply.
            if(!impact.missileDamage->statRules) throw std::runtime_error("Missing native missile accuracy stat definitions");
            const auto &rules=*impact.missileDamage->statRules;
            const auto owner=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
            const auto defender=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
            if(!owner || !defender) throw std::runtime_error("Missing native missile accuracy sources");
            const auto read=[&](const StatSnapshot &values,std::string_view name){return values.get({rules.names.at(std::string(name)),0});};
            const int level=read(*owner,"level"),defenderLevel=read(*defender,"level");
            const auto defense=nativeAdd(target.player?target.player->totals.character.defense:ports_.effects.unitDefense(target.id(),tick.tick),read(*defender,"armorclass_vs_missile"));
            int chance{};
            if(source.monster) {
                const auto rating=nativeAdd(impact.monsterRating,nativeAdd(nativeMultiply(5,read(*owner,"dexterity")),read(*owner,"tohit")));
                chance=physicalHitChance(level,nativeAdd(rating,nativePercentage(rating,read(*owner,"item_tohit_percent"),100)),defenderLevel,defense);
            } else {
                auto percent=nativeAdd(impact.monsterRating,read(*owner,"item_tohit_percent"));
                if(target.monster && impact.missileDamage->monsterTypes) {
                    const auto types=impact.missileDamage->monsterTypes->classes.find(target.monster->rule.nativeClass);
                    if(types!=impact.missileDamage->monsterTypes->classes.end()) {
                        const auto id=rules.names.at("attack_vs_montype");size_t count{};
                        for(const auto &[key,value]:owner->values) if(key.id==id && count++<128 && key.layer &&
                            std::binary_search(types->second.begin(),types->second.end(),key.layer)) percent=nativeAdd(percent,value);
                    }
                }
                AttackTargetModifiers modifiers;
                modifiers.ignoreDefense=read(*owner,"item_ignoretargetac")!=0;
                modifiers.defenseReduction=read(*owner,"item_fractionaltargetac");
                modifiers.demonAttackRating=read(*owner,"item_demon_tohit");modifiers.undeadAttackRating=read(*owner,"item_undead_tohit");
                const auto factor=source.player->definition.toHitFactor;
                chance=weaponHitChance(level,unitAttackRating(read(*owner,"dexterity"),read(*owner,"tohit"),factor),percent,modifiers,
                    {defenderLevel,defense,target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead,
                        target.monster && target.monster->rule.boss,bool(target.player),target.monster && target.monster->hireling},
                    target.monster?target.monster->identity.rank:MonsterRank::Normal);
            }
            contact.hit=int(rollRandom(random)%100)<chance;
        }
        if(source.id()) {const auto result=publishSeed(source,expected,random);if(result!=DomainStatus::Applied) return result;}
        contact.accuracyPrepared=true;
    }
    if(!contact.delayPrepared && impact.nativeNextHit) {
        const auto result=impact.pd2NextHitKey?ports_.effects.pd2MissileHitDelay(target.id(),*impact.pd2NextHitKey,impact.nextDelay,tick.tick):
            ports_.effects.nativeMissileHitDelay(target.id(),*impact.nativeNextHit,impact.nextDelay,tick.tick);
        if(!result) return result.status;
        contact.delayPrepared=true;
    }
    if(!(impact.nativeSkill && impact.nativeSkill->melee) && impact.missileEventTarget!=impact.next) {
        if(!impact.missileEvents) impact.missileEvents=std::make_shared<effects::NativeContactBatch>(
            ports_.effects.prepareMissileEvents(impact.source,target.id(),tick.tick,impact.returnFire));
        const auto result=ports_.effects.advanceDamageEvents(*impact.missileEvents,tick.tick);
        if(!result) return result.status;
        impact.missileEventTarget=impact.next;impact.missileEvents.reset();
        const Participants participants{ports_.players,ports_.monsters,ports_.social};
        source=participants.find(impact.source);target=participants.find(impact.targets[impact.next]);
        if(!target.id()) return DomainStatus::Stale;
    }
    if(!preludeOnly && target.monster) ports_.monsters.storeLastAttacker(target.id(),source.id());
    if(preludeOnly || !contact.hit) return DomainStatus::Applied;
    if(!source.id()) return DomainStatus::Unavailable;
    if(impact.nativeSkill) {
        const auto prepared=prepareNativeSkillContact(impact,source,target,tick);
        if(prepared!=DomainStatus::Applied) return prepared;
        if(impact.areaDamage && impact.areaDamage->rolled && impact.areaDamage->rolled->value)
            impact.hitClass=impact.areaDamage->rolled->value->hitClass&0xf;
        contact.hit=impact.nativeSkill->hit;
        if(!contact.hit) return DomainStatus::Applied;
        const Participants participants{ports_.players,ports_.monsters,ports_.social};
        source=participants.find(impact.source);target=participants.find(impact.targets[impact.next]);
        if(!source.id() || !target.id()) return DomainStatus::Stale;
    }
    if(!contact.damagePrepared) {
        const auto criticalCaster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
        const bool pd2Missile=impact.allocatedMissile && criticalCaster && criticalCaster->programs() && criticalCaster->programs()->pd2;
        if(pd2Missile) {const auto result=preparePd2MissileCritical(impact,source,tick);if(result!=DomainStatus::Applied) return result;}
        const auto *monster=target.monster;
        int32_t typeBonus{};
        if(monster && impact.missileDamage->monsterTypes) {
            const auto found=impact.missileDamage->monsterTypes->classes.find(monster->rule.nativeClass);
            if(found!=impact.missileDamage->monsterTypes->classes.end()) for(const auto &[layer,value]:impact.missileDamage->monsterTypeBonuses)
                if(layer && std::binary_search(found->second.begin(),found->second.end(),layer)) typeBonus=nativeAdd(typeBonus,value);
        }
        auto &missile=*impact.missileDamage;
        const MissileDamageTarget damageTarget{true,monster && monster->rule.demon,monster && monster->rule.undead,typeBonus};
        auto data=missile.data;if(pd2Missile) data.flags&=~uint32_t{2};
        if(impact.areaDamage && !impact.areaDamage->rolled) impact.areaDamage->rolled=std::make_shared<MissileAreaRoll>();
        if(impact.areaDamage && !impact.areaDamage->rolled->value)
            impact.areaDamage->rolled->value=impact.areaDamage->full?rollMissileDamageData(data,missile.mastery,damageTarget,missile.random):
                rollMissileElementDamage(data,impact.areaDamage->element,missile.mastery,damageTarget,missile.random).hit;
        auto rolled=impact.areaDamage?*impact.areaDamage->rolled->value:rollMissileDamageData(data,missile.mastery,damageTarget,missile.random);
        if(pd2Missile && contact.pd2Critical->kind) {
            rolled.critical=true;
            rolled.channels[0]=nativeMultiplyDivide(rolled.channels[0],contact.pd2Critical->percentage,100);
        }
        if(impact.areaDamage) {
            const auto flags=impact.areaDamage->hitFlags;
            const auto result=impact.areaDamage->resultFlags;
            rolled.bypassUndead|=(flags&0x100)!=0;rolled.bypassDemon|=(flags&0x200)!=0;rolled.bypassBeasts|=(flags&0x400)!=0;
            rolled.knockback|=(result&8)!=0;rolled.softHit|=(result&0x4000)!=0;rolled.critical|=(result&0x2000)!=0;
            contact.resultSuccessful=(result&1)!=0;
        }
        if(impact.areaDamage && impact.areaDamage->length && *impact.areaDamage->length>0) {
            const auto length=*impact.areaDamage->length;
            switch(impact.areaDamage->element) {
            case MissileDamageElement::Cold:rolled.coldLength=length;break;
            case MissileDamageElement::Freeze:rolled.freezeLength=length;break;
            case MissileDamageElement::Poison:rolled.poisonLength=length;break;
            case MissileDamageElement::Burn:rolled.burnLength=length;break;
            case MissileDamageElement::Stun:rolled.stunLength=length;break;
            default:break;
            }
        }
        MissileDamageProgramInput program;
        program.function=missile.serverDamageFunction;program.element=missile.element;program.parameters=missile.damageParameters;
        const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
        const bool pd2=caster && caster->programs() && caster->programs()->pd2;
        program.level=missile.level;program.ownerHireling=source.monster && source.monster->hireling;
        program.targetMonster=bool(monster);program.targetPlayer=bool(target.player);
        if(monster && missile.monsterTypes) {
            const auto dimensions=missile.monsterTypes->dimensions.find(monster->rule.nativeClass);
            if(dimensions!=missile.monsterTypes->dimensions.end()) {
                program.targetDimensionsKnown=true;program.targetSmall=dimensions->second.first;program.targetLarge=dimensions->second.second;
            }
        }
        if(!impact.areaDamage && !impact.nativeDamageFlagsOnly && (program.function==1 || program.function==7 || program.function==9 || program.function==11 || program.function==12 || program.function==13 || (pd2 && (program.function==15 || program.function==16 || program.function==17)))) {
            if(!missile.formulas || !missile.statRules) throw std::runtime_error("Missing native missile damage program definitions");
            const auto owner=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
            if(!owner) throw std::runtime_error("Missing native missile damage program owner");
            const auto expected=seed(source);auto random=expected;
            const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
                if(low>=high) return low;
                return nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
            };
            auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*owner,draw);
            SkillFormulaValues values(*missile.formulas,input);
            if(const auto skill=missile.formulas->skills.find(missile.skill);skill!=missile.formulas->skills.end()) {
                program.skillExists=true;program.skillParameter1=skill->second.number("param1");program.skillParameter2=skill->second.number("param2");
            }
            if(program.function==1 || program.function==12 || (pd2 && program.function==17)) {
                const auto percentage=values.calculation(FormulaDomain::Missile,missile.definition,missile.level,"dmgcalc1",std::pair{-1,-1});
                if(!percentage) throw std::runtime_error("Unavailable native missile damage formula context");
                program.percentage=*percentage;
            }
            if(program.function==11) {
                if(!missile.targetParameterX) throw std::runtime_error("Rabies missile lacks native expiration parameter");
                program.remainingPoison=nativeSubtract(*missile.targetParameterX,nativeNarrow(tick.tick));
                const auto maximum=values.parameter(FormulaDomain::Skill,missile.skill,missile.level,"edln");
                if(!maximum) throw std::runtime_error("Unavailable native Rabies length");
                program.maximumPoisonLength=*maximum;
            }
            program.ownerDamagePercent=owner->get({missile.statRules->names.at("damagepercent"),0});
            if(pd2 && (program.function==15 || program.function==16)) {
                const auto first=values.calculation(FormulaDomain::Skill,missile.skill,missile.level,program.function==15?"calc1":"calc2");
                const auto second=values.calculation(FormulaDomain::Skill,missile.skill,missile.level,program.function==15?"calc2":"calc3");
                if(!first || !second) throw std::runtime_error("Unavailable PD2 missile damage formula");
                program.skillParameter1=*first;program.skillParameter2=*second;
            }
            if(random!=expected) contact.damageFormulaSeed=std::pair{expected,random};
        }
        if(!impact.areaDamage && !impact.nativeDamageFlagsOnly && !applyMissileDamageProgram(rolled,program,missile.data,missile.mastery,damageTarget,missile.random))
            throw std::runtime_error("Unavailable native missile damage program");
        if(pd2 && !impact.areaDamage && !impact.nativeDamageFlagsOnly) {
            // Current core RVAs 0x3A48A0 / 0x3A49B0. Psychic Hammer's
            // bounce decay uses signed products; Double Throw's bonus uses
            // the helper's unsigned product/division, including overflow.
            const auto targetX=missile.targetParameterX.value_or(0);
            if(program.function==15) {
                const auto decay=nativeMultiply(nativeSubtract(program.skillParameter1,targetX),program.skillParameter2);
                rolled.channels[1]=decay>99?0:nativeMultiply(nativeSubtract(100,decay),rolled.channels[1])/100;
            } else if(program.function==16 && program.skillParameter1==targetX && program.skillParameter2>0) {
                for(auto &channel:rolled.channels)
                    channel=int32_t(uint32_t(nativeMultiply(nativeAdd(program.skillParameter2,100),channel))/100u);
                for(auto *drain:{&rolled.lifeLeech,&rolled.manaLeech,&rolled.staminaLeech})
                    *drain=int32_t(uint32_t(nativeMultiply(nativeAdd(program.skillParameter2,100),*drain))/100u);
            } else if(program.function==17 && missile.pierceCount>1) {
                // Current 0x3A4AB0 and collision helper 0x302B20. Only
                // successful native pierce continuations advance this stat.
                const auto factor=std::max(double(uint32_t(program.parameters[0])),
                    100.0-double(nativeSubtract(missile.pierceCount,1))*program.percentage);
                const auto scale=[&](int32_t value) {
                    const double amount=double(value)*factor/100.0;
                    return amount>INT32_MAX?INT32_MAX:amount<INT32_MIN?INT32_MIN:int32_t(std::trunc(amount));
                };
                for(auto &channel:rolled.channels) channel=scale(channel);
                for(auto *drain:{&rolled.lifeLeech,&rolled.manaLeech,&rolled.staminaLeech}) *drain=scale(*drain);
            } else if(program.function==19) {
                // Current core RVA 0x3A4D40: percentage drains use unsigned
                // products. The strict CRT roll sets ResultFlags 0x20,
                // suppressing both hit and kill events without removing XP.
                for(auto *drain:{&rolled.lifeLeech,&rolled.manaLeech,&rolled.staminaLeech})
                    *drain=int32_t(uint32_t(nativeMultiply(*drain,program.parameters[1]))/100u);
                contact.suppressEvents=int(pd2CrtRandom(ports_.pd2Random)%100)>program.parameters[0];
                if(contact.suppressEvents && targetX) rolled.lifeLeech=rolled.manaLeech=rolled.staminaLeech=0;
            }
        }
        impact.missileRolled=std::pair{impact.next,rolled};
        contact.damage=impact.monsterHit.value_or(MonsterHit{});
        for(size_t i=0;i<rolled.channels.size();++i) contact.damage.channels[i]=rolled.channels[i];
        contact.damage.coldFrames=uint64_t(std::max(0,rolled.coldLength));
        contact.damage.freezeFrames=uint64_t(std::max(0,rolled.freezeLength));
        impact.freeze=rolled.freezeLength>0;
        contact.damage.poisonFrames=uint64_t(std::max(0,rolled.poisonLength));
        contact.damage.stunFrames=uint64_t(std::max(0,rolled.stunLength));
        contact.damage.burn=rolled.burn;contact.damage.burnFrames=uint64_t(std::max(0,rolled.burnLength));
        contact.damage.critical=rolled.critical;
        contact.damage.softHit=rolled.softHit;contact.damage.knockback|=rolled.knockback;
        contact.damage.hitClass=uint8_t((rolled.hitClass&0xf0)|impact.hitClass);
        // Percent leech and direct monster drains share native stat IDs, but
        // are interpreted at the source-type boundary by the HP execution.
        contact.damage.lifeDrain=rolled.lifeLeech;
        contact.damage.mana=rolled.manaLeech;contact.damage.stamina=rolled.staminaLeech;
        contact.damagePrepared=true;
    }
    if(contact.damageFormulaSeed) {
        const auto result=publishSeed(source,contact.damageFormulaSeed->first,contact.damageFormulaSeed->second);
        if(result!=DomainStatus::Applied) return result;
        contact.damageFormulaSeed.reset();
    }
    if(!contact.damageProgramDomainPrepared) {
        const auto &missile=*impact.missileDamage;
        if(!impact.areaDamage && !impact.nativeDamageFlagsOnly && missile.serverDamageFunction==8 && missile.programOverlay>0 && !ports_.events.publish({0,tick.tick,{},
            {AudienceKind::Area,{},target.player?target.player->area:target.monster->area},
            {OverlayFact{target.id(),uint8_t(target.player?0:1),target.player?target.player->area:target.monster->area,missile.programOverlay}}})) return DomainStatus::Capacity;
        if(impact.missileRolled->second.disableTreasure && target.monster) ports_.monsters.disableTreasure(target.id());
        contact.damageProgramDomainPrepared=true;
    }
    if(!contact.defensePrepared) {
        if(!contact.flagsPrepared) {
            auto &missile=*impact.missileDamage;
            bool uninterruptable{};
            if(target.player && target.player->transient.effects) uninterruptable=target.player->transient.effects->hasState(missile.uninterruptableState,tick.tick);
            else if(target.monster) uninterruptable=ports_.effects.unitStates(target.id(),tick.tick).contains(missile.uninterruptableState) ||
                target.monster->aiStates.contains(missile.uninterruptableState);
            contact.damage.getHit=(impact.areaDamage && !impact.nativeMissileFlags?(impact.areaDamage->resultFlags&4)!=0:missile.getHit) && !uninterruptable;
            if(!uninterruptable) {
                contact.damage.softHit|=(!impact.areaDamage || impact.nativeMissileFlags) && !missile.getHit && missile.softHit;
                if((!impact.areaDamage || impact.nativeMissileFlags) && missile.knockbackChance && rollRandom(missile.random)%100<missile.knockbackChance) contact.damage.knockback=true;
            }
            contact.flagsPrepared=true;
        }
        if(impact.nativeSkill && impact.nativeSkill->directFunction==163) {
            const auto defense=preparePd2SplashDefense(impact,source,target,tick);if(defense!=DomainStatus::Applied) return defense;
        }
    }
    if(!contact.defensePrepared) {
        const auto expected=seed(target);auto random=expected;
        auto modifiers=target.player?target.player->totals.character.combat:target.monster->petStats.attributes.combat;
        contact.blocked=false;contact.avoidance=WeaponAvoidance::None;
        if(!impact.unblockable) {
            const auto values=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
            if(!values || !impact.missileDamage->statRules) throw std::runtime_error("Missing native missile block/avoidance sources");
            const auto &rules=*impact.missileDamage->statRules;
            const auto stat=[&](std::string_view name){return values->get({rules.names.at(std::string(name)),0});};
            modifiers.avoid=stat("passive_avoid");modifiers.evade=stat("passive_evade");modifiers.dodge=stat("passive_dodge");
            // Native missile block is enabled by nonzero physical damage.
            int block=target.player && target.player->totals.equipment.shield?target.player->totals.equipment.blockChance:
                target.monster && (target.monster->shield || target.monster->rule.blockWithoutShield || target.monster->rule.nativeBlockException)?std::min(75,stat("toblock")):0;
            if(target.player && target.player->moving && target.player->runningNow) block/=3;
            int weaponBlock{};bool dualClaws{};
            if(target.player) {
                const auto &equipment=target.player->totals.equipment;dualClaws=equipment.animationClass=="ht2";
                weaponBlock=nativeWeaponBlockChance(equipment,*values,rules);
            } else if(target.monster->equipmentStats) {
                const auto &equipment=*target.monster->equipmentStats;dualClaws=equipment.animationClass=="ht2";
                weaponBlock=nativeWeaponBlockChance(equipment,*values,rules);
            }
            const auto defense=rollNativeBlockOrDodge(block,(!impact.areaDamage || impact.nativeMissileFlags) && contact.damage.channels[0]!=0,target.moving(),weaponBlock,dualClaws,modifiers,true,random);
            contact.blocked=defense.block;contact.avoidance=defense.avoidance;
        }
        const auto result=publishSeed(target,expected,random);if(result!=DomainStatus::Applied) return result;
        contact.defensePrepared=true;
        if((contact.blocked || contact.avoidance!=WeaponAvoidance::None) && (!impact.areaDamage || impact.nativeMissileFlags) && !impact.missileDamage->getHit && !impact.missileDamage->knockbackChance) {
            contact.damage.softHit=true;contact.reactionPrepared=true;
        }
    }
    if(!contact.criticalPrepared) {
        if(source.monster && source.monster->rule.criticalChance) {
            const auto expected=seed(source);auto random=expected;auto damage=contact.damage;
            // ApplyCriticalDamage runs even if block/dodge removed success.
            monsterCritical(damage,source.monster->rule.criticalChance,random);
            const auto result=publishSeed(source,expected,random);if(result!=DomainStatus::Applied) return result;
            contact.damage=std::move(damage);
        }
        if(contact.blocked) ports_.itemTriggers.schedulePd2Block(target.id(),source.id(),tick.tick);
        contact.criticalPrepared=true;
    }
    if(!contact.reactionPrepared) {
        DomainStatus result=DomainStatus::Applied;
        if(contact.blocked && !contact.damage.softHit) result=target.player?ports_.effects.block({target.player->player,target.id(),target.player->area,generation,0,tick.tick}).status:
            ports_.monsters.block(target.id(),tick.tick).status;
        else if(contact.avoidance!=WeaponAvoidance::None && target.player)
            result=ports_.effects.avoidance({target.player->player,target.id(),target.player->area,generation,0,tick.tick},contact.avoidance,source.id()).status;
        if(result==DomainStatus::Capacity) return result;
        contact.reactionPrepared=true;
    }
    // AllocCombat only retains damage/reception. ExecuteEvents and item
    // procedures belong to DrainItemDurability, at the later action flag.
    if(impact.nativeSkill && impact.nativeSkill->melee) return DomainStatus::Applied;
    return prepareContactProcedures(impact,source,target,tick);
}
DomainStatus System::prepareContactProcedures(SpellImpact &impact,ParticipantView source,ParticipantView target,TickContext tick) {
    auto &contact=*impact.missileContact;
    if(!contact.proceduresPrepared && contact.resultSuccessful && !contact.blocked && contact.avoidance==WeaponAvoidance::None) {
        const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
        WeaponHitEffects extras;WeaponHitResult playerExtras;std::optional<OpenWoundsApplication> pd2Wound;
        if(impact.weapon && !contact.suppressEvents) {
            auto attack=*impact.weapon;
            const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
            const bool pd2=caster && caster->programs() && caster->programs()->pd2;
            const auto crushing=pd2?pd2CrushingBlow(ports_,impact,source,target,tick.tick,random):0;
            if(pd2) {
                attack.crushingBlow=attack.openWounds=0;
                pd2Wound=pd2OpenWounds(ports_,impact,source,target,tick.tick,random);
            }
            // Native percentage drains are executed by advanceMissileDrain.
            // CB / OW remain weapon procedures and do not replace the ranges.
            attack.lifeLeech=attack.manaLeech=0;
            if(target.monster) extras=weaponHitEffects(attack,*target.monster,source.id(),impact.binding.credit,
                ports_.effects.unitResistance(target.id(),DamageType::Physical,tick.tick),0,{},tick.tick,random);
            else playerExtras=calculateWeaponHitEffects(attack,{target.player->persistent.player.hp.raw(),
                target.player->totals.character.combat.physicalResist,100,1,false,false,false,true},0,{},random);
            if(pd2) {extras.crushing=crushing;playerExtras.crushing=crushing;extras.wound=pd2Wound;}
        }
        reactions::ItemEventPlan events;
        if(!contact.suppressEvents && (source.player || (source.monster && ports_.monsters.skillPrograms() && ports_.monsters.skillPrograms()->pd2))) {
            const auto region=source.player?source.player->area:source.monster->area;
            const auto *sourceArea=ports_.areas.find(region);if(!sourceArea) return DomainStatus::Stale;
            const ActorContext actor{source.player?source.player->player:source.monster->owner.value_or(PlayerId{}),source.id(),region,sourceArea->generation,0,tick.tick};
            auto prepared=ports_.itemTriggers.prepare(actor,(impact.weapon || impact.melee) && !impact.suppressSourceEvents?std::initializer_list{ItemSkillEvent::Hit,ItemSkillEvent::Kill}:
                std::initializer_list{ItemSkillEvent::Kill},target.id(),target.position());
            if(!prepared) return prepared.status;
            events=std::move(*prepared.value);
            if(!impact.suppressSourceEvents) {
                auto healing=ports_.itemTriggers.prepare(actor,{ItemSkillEvent::HitHeal},target.id(),target.position());
                if(!healing) return healing.status;
                ports_.itemTriggers.append(events,std::move(*healing.value));
            }
        }
        const auto result=source.monster?ports_.monsters.consumeRandom(source.id(),expected,random).status:
            ports_.transactions.consumeRandom({source.player->player,source.id(),source.player->area,
                ports_.areas.at(source.player->area).generation,0,tick.tick},expected,random).status;
        if(result!=DomainStatus::Applied) return result;
        contact.extras=std::move(extras);contact.pd2Wound=std::move(pd2Wound);contact.playerExtras=playerExtras;contact.itemEvents=std::move(events);
        contact.proceduresPrepared=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::prepareMeleeDrainProcedures(SpellImpact &impact,ParticipantView target,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee) return DomainStatus::Applied;
    if(impact.nativeSkill->directFunction==163 || impact.nativeSkill->directFunction==153) {impact.nativeSkill->proceduresTransferred=true;return DomainStatus::Applied;}
    impact.areaDamage->hitFlags=0x20; // DrainItemDurability's local damage copy.
    const auto source=Participants{ports_.players,ports_.monsters,ports_.social}.find(impact.source);
    if(!source.id()) return DomainStatus::Stale;
    if(!impact.nativeSkill->proceduresTransferred) {
        const auto result=prepareContactProcedures(impact,source,target,tick);if(result!=DomainStatus::Applied) return result;
        auto &contact=*impact.missileContact;
        if(impact.playerContact) {
            auto &execution=*impact.playerContact;
            execution.extras=contact.playerExtras;execution.itemEvents=std::move(contact.itemEvents);
            execution.adjustment.crushing=contact.playerExtras.crushing;
            if(contact.pd2Wound) execution.adjustment.wound=contact.pd2Wound;
            else if(contact.playerExtras.woundRate>0) execution.adjustment.wound=OpenWoundsApplication{
                contact.playerExtras.woundRate,source.id(),impact.binding.credit,tick.tick+200,tick.tick+1};
            execution.remainingLife=std::max<int64_t>(0,int64_t(target.player->persistent.player.hp.raw())-contact.playerExtras.crushing);
            execution.victim.tick=execution.attacker.tick=tick.tick;
        } else if(impact.monsterContact) {
            impact.monsterContact->extras=contact.extras;impact.monsterContact->itemEvents=std::move(contact.itemEvents);
        }
        impact.nativeSkill->proceduresTransferred=true;
    }
    auto &execution=*impact.nativeSkill;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
    if(!execution.splashItemsComplete && !impact.suppressSourceEvents && caster && caster->programs() && caster->programs()->pd2) {
        if(!execution.splashItems) {
            const auto weaponStats=ports_.effects.unitStatValues(source.id(),tick.tick,
                impact.weapon && impact.weapon->weapon.item?std::optional{impact.weapon->weapon.item}:std::nullopt);
            if(!weaponStats) return DomainStatus::Unavailable;
            auto events=ports_.itemTriggers.prepare(caster->context(ports_.areas,tick.tick),{ItemSkillEvent::MeleeSplash},
                target.id(),target.position(),&*weaponStats);
            if(!events) return events.status;
            execution.splashItems=std::make_shared<reactions::ItemEventPlan>(std::move(*events.value));
        }
        if(ports_.itemTriggers.advance(*execution.splashItems,tick.tick)==StepStatus::Blocked) return DomainStatus::Capacity;
        execution.splashItemsComplete=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::finishMissileArmor(SpellImpact &impact,ParticipantView target,TickContext tick) {
    auto &contact=*impact.missileContact;
    if(contact.armorCommitted) return DomainStatus::Applied;
    if(impact.nativeSkill && impact.nativeSkill->melee && impact.nativeSkill->directFunction!=163 && impact.nativeSkill->directFunction!=153) {
        if(!contact.weaponWearCommitted) {
            if(const auto *player=ports_.players.findActor(impact.source)) {
            const auto &attack=*impact.weapon;const auto item=player->persistent.inventory.items.find(attack.weapon.item);
            if(item!=player->persistent.inventory.items.end() && attack.wearChance>0) {
                const auto expected=player->combatRandom;auto random=expected;
                const auto &properties=player->rules.equipment->at(item->first,player->persistent.player.level);
                const bool destructible=properties.maximumDurability && item->second.durability &&
                    std::none_of(properties.stats.begin(),properties.stats.end(),[](const auto &stat){return stat.effect=="item_indesctructible" && stat.value;});
                const bool wear=destructible && limitedRandom(random,100)<unsigned(attack.wearChance);
                const ActorContext actor{player->player,player->actor,player->area,ports_.areas.at(player->area).generation,0,tick.tick};
                if(wear) {
                    auto plan=ports_.inventory.weaponCost(actor,attack.weapon,attack.wearSkill,false,false,unsigned(std::max(attack.wearAmount,1)));
                    if(!plan) return plan.status;
                    plan.value->random=transactions::UnitRandomEdit{expected,random};
                    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result.status;
                } else {
                    const auto result=ports_.transactions.consumeRandom(actor,expected,random);if(!result) return result.status;
                }
            }
            }
            contact.weaponWearCommitted=true;
        }
        if(!contact.defenderWearCommitted) {
            if(target.player) {
                const auto &player=*target.player;const auto expected=player.combatRandom;auto random=expected;
                const auto wear=ports_.inventory.defensiveWear(player.player,random);
                const ActorContext actor{player.player,player.actor,player.area,ports_.areas.at(player.area).generation,0,tick.tick};
                if(wear) {
                    auto plan=ports_.inventory.wearCost(actor,*wear);if(!plan) return plan.status;
                    plan.value->random=transactions::UnitRandomEdit{expected,random};
                    const auto result=ports_.transactions.commit(std::move(*plan.value));if(!result) return result.status;
                } else {
                    const auto result=ports_.transactions.consumeRandom(actor,expected,random);if(!result) return result.status;
                }
            }
            contact.defenderWearCommitted=true;
        }
    }
    const auto adjustment=impact.missileDamage->data.targetArmor;
    if(target.monster && !target.monster->hireling && !impact.areaDamage && adjustment) {
        const auto &actor=*target.monster;
        if(actor.revision==UINT64_MAX) return DomainStatus::Capacity;
        attributes::MonsterResourceTrace resources(actor,tick.tick);
        const auto sources=ports_.effects.unitStatSources(actor.id,tick.tick);resources.sampleSources(sources);
        const auto current=resources.values(sources).get({actor.rule.stats->names.at("armorclass"),0});
        resources.writeBaseStat("armorclass",std::max(0,nativeAdd(current,adjustment)),sources);
        auto change=resources.take();
        if(!ports_.monsters.canCommitLife(change)) return DomainStatus::Stale;
        if(!ports_.monsters.commitLife(std::move(change))) throw std::logic_error("Missile target armor lost its source reservation");
    }
    contact.armorCommitted=true;return DomainStatus::Applied;
}

}
