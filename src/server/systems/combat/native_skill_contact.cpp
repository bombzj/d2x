#include "system.hpp"
#include "player_contact.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/skills/evaluation.hpp"
#include "server/systems/skills/native_caster.hpp"
#include "server/systems/reactions/item_triggers.hpp"
#include "gameplay/skills/native_damage.hpp"
#include "gameplay/skills/formula_parameters.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include "gameplay/skills/druid_spec.hpp"
#include "gameplay/combat/weapon_block.hpp"
#include "gameplay/combat/target_bonus.hpp"
#include "gameplay/combat/geometry.hpp"
#include "gameplay/skills/assassin_progressive.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x::server::combat {
bool System::addAllocatedPhysical(EntityId source,uint64_t occurrence,int32_t amount) {
    for(auto &impact:state_.spells) if(impact.source==source && impact.occurrence==occurrence && impact.nativeDrainHeld && impact.nativeSkill && impact.nativeSkill->heldPrepared) {
        if(impact.playerContact) {
            auto &damage=impact.playerContact->defense.damage;
            damage.channels[0].dealt=nativeAdd(nativeChecked(damage.channels[0].dealt),amount);
            damage.dealt=nativeAdd(nativeChecked(damage.dealt),amount);
        } else if(impact.monsterContact) {
            auto &contact=*impact.monsterContact;contact.physical=nativeAdd(nativeChecked(contact.physical),amount);
            contact.amount=nativeAdd(nativeChecked(contact.amount),amount);
        } else return false;
        impact.nativeSkill->allocatedPhysical=nativeAdd(impact.nativeSkill->allocatedPhysical,amount);return true;
    }
    return false;
}
DomainResult<bool> System::sampleNativeSkillAccuracy(const ActorContext &actor,const SkillCastSpec &skill,const WeaponDamage &weapon,EntityId victim,std::shared_ptr<NativeSkillContact> &cursor) {
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(actor.actor),target=participants.find(victim);
    if(!source.aliveIn(actor.area) || !target.aliveIn(actor.area) || !participants.canHarm(actor.actor,victim)) return {DomainStatus::Unavailable,{}};
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,actor.actor);
    if(!caster || !caster->programs()) return {DomainStatus::InvalidActor,{}};
    if(!cursor) cursor=std::make_shared<NativeSkillContact>();
    cursor->melee=cursor->accuracyOnly=true;cursor->skill=skill.sourceId;cursor->rank=skill.rank;
    SpellImpact impact;impact.source=actor.actor;impact.area=actor.area;impact.targets={victim};impact.nativeSkill=cursor;
    const auto modifiers=source.player?source.player->totals.character.combat:ports_.effects.combatModifiers(source.id(),actor.tick);
    impact.weapon=prepareWeaponSkillProcedures(weapon,modifiers,skill,caster->level(),false);
    impact.missileDamage=std::make_shared<MissileDamageRollState>();impact.missileDamage->formulas=caster->programs()->formulas;
    const auto prepared=prepareNativeSkillContact(impact,source,target,{actor.tick});
    return {prepared,prepared==DomainStatus::Applied?std::optional{cursor->hit}:std::nullopt};
}
DomainResult<bool> System::prepareWeaponBlock(ParticipantView target,TickContext tick,WeaponBlockCursor &cursor) {
    if(cursor.target!=target.id()) cursor={target.id()};
    if(!cursor.checked) {
        const auto *gear=target.player?&target.player->totals.equipment:
            target.monster && target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
        if(!gear || gear->animationClass!="ht2" || target.moving()) {cursor.checked=true;return {DomainStatus::Applied,false};}
        const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
        const auto *rules=target.player?target.player->rules.character->stats.get():target.monster->rule.stats.get();
        if(!stats || !rules) return {DomainStatus::Unavailable,{}};
        const int chance=nativeWeaponBlockChance(*gear,*stats,*rules);
        if(chance>0) {
            const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
            const bool blocked=int(rollRandom(random)%100)<chance;
            const auto consumed=target.player?ports_.transactions.consumeRandom(
                {target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},expected,random):
                ports_.monsters.consumeRandom(target.id(),expected,random);
            if(!consumed) return {consumed.status,{}};
            cursor.blocked=blocked;
        }
        cursor.checked=true;
    }
    if(cursor.blocked && !cursor.reactionComplete) {
        const auto result=target.player?ports_.effects.block({target.player->player,target.id(),target.player->area,
            ports_.areas.at(target.player->area).generation,0,tick.tick}):ports_.monsters.block(target.id(),tick.tick);
        if(!result) return {result.status,{}};
        cursor.reactionComplete=true;
    }
    return {DomainStatus::Applied,cursor.blocked};
}
DomainStatus System::prepareNativeSkillContact(SpellImpact &impact,ParticipantView source,ParticipantView target,TickContext tick) {
    auto &execution=*impact.nativeSkill;const auto &catalog=*impact.missileDamage->formulas;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,source.id());
    if(!caster || !caster->programs()) return DomainStatus::InvalidActor;
    const auto &programs=*caster->programs();
    const auto sourceStats=[&]() {
        if(source.player) {
            const auto hand=impact.weapon?source.player->totals.handStats.find(impact.weapon->weapon.item):source.player->totals.handStats.end();
            return std::optional<StatSnapshot>{hand==source.player->totals.handStats.end()?source.player->totals.nativeStats:hand->second};
        }
        const auto hand=impact.weapon && impact.weapon->weapon.item?std::optional{impact.weapon->weapon.item}:std::nullopt;
        return ports_.effects.unitStatValues(source.id(),tick.tick,hand);
    };
    const auto typeBonus=[&](std::string_view name) {
        if(!target.monster || !programs.monsterTypes) return int32_t{};
        const auto &types=programs.monsterTypes->classes;
        const auto found=types.find(target.monster->rule.nativeClass);if(found==types.end()) return int32_t{};
        const auto stats=sourceStats();if(!stats) return int32_t{};
        return nativeMonsterTypeBonus(*stats,programs.stats->names.at(std::string(name)),found->second);
    };
    const auto consume=[&](uint64_t expected,uint64_t next) {
        if(next==expected) return DomainStatus::Applied;
        if(source.monster) return ports_.monsters.consumeRandom(source.id(),expected,next).status;
        const auto &player=*source.player;
        return ports_.transactions.consumeRandom({player.player,player.actor,player.area,ports_.areas.at(player.area).generation,0,tick.tick},expected,next).status;
    };
    const auto evaluate=[&](uint64_t &random,const auto &operation) {
        const auto stats=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
        if(!stats) throw std::logic_error("Missing native direct skill owner stats");
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*stats,draw);
        SkillFormulaValues values(catalog,input);return operation(values);
    };
    if(execution.melee) {
        if(!impact.weapon) return DomainStatus::Unavailable;
        if(!execution.accuracyPrepared) {
            const auto expected=caster->random();auto random=expected;
            auto attack=*impact.weapon;
            attack.weapon.attackRatingPercent=nativeAdd(attack.weapon.attackRatingPercent,typeBonus("attack_vs_montype"));
            if(execution.progressive) {
                const auto stats=sourceStats();if(!stats) return DomainStatus::Unavailable;
                attack.weapon.attackRatingPercent=nativeAdd(attack.weapon.attackRatingPercent,stats->get({programs.stats->names.at("progressive_tohit"),0}));
            }
            const bool alive=source.player?source.player->persistent.player.hp>0:source.monster->alive();
            const bool hit=alive && weaponContact(attack,target.id(),tick.tick,random);
            const auto result=consume(expected,random);if(result!=DomainStatus::Applied) return result;
            execution.hit=hit;execution.accuracyPrepared=true;
        }
        if(execution.hit && !execution.defensePrepared) {
            const auto expected=target.player?target.player->combatRandom:target.monster->combatRandom;auto random=expected;
            const auto stats=target.player?std::optional{target.player->totals.nativeStats}:ports_.effects.unitStatValues(target.id(),tick.tick);
            if(!stats) return DomainStatus::Unavailable;
            const auto &rules=target.player?*target.player->rules.skills->stats:*target.monster->rule.stats;
            int block=target.player && target.player->totals.equipment.shield?target.player->totals.equipment.blockChance:
                target.monster && (target.monster->shield || target.monster->rule.blockWithoutShield || target.monster->rule.nativeBlockException)?
                    std::min(75,stats->get({rules.names.at("toblock"),0})):0;
            if(target.player && target.player->moving && target.player->runningNow) block/=3;
            const auto mods=target.player?target.player->totals.character.combat:ports_.effects.combatModifiers(target.id(),tick.tick);
            const auto *equipment=target.player?&target.player->totals.equipment:
                target.monster->equipmentStats?&*target.monster->equipmentStats:nullptr;
            const auto defense=rollNativeBlockOrDodge(block,true,target.moving(),
                equipment?nativeWeaponBlockChance(*equipment,*stats,rules):0,
                equipment && equipment->animationClass=="ht2",mods,false,random);
            const auto result=target.monster?ports_.monsters.consumeRandom(target.id(),expected,random).status:
                ports_.transactions.consumeRandom({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},expected,random).status;
            if(result!=DomainStatus::Applied) return result;
            execution.defense=defense;execution.hit=!defense.block && defense.avoidance==WeaponAvoidance::None;execution.defensePrepared=true;
        }
        if(execution.accuracyOnly) return DomainStatus::Applied;
        if(!execution.hit) {
            if(impact.nativeDrainHeld || execution.skipMissDrain) return DomainStatus::Applied;
            if(!nativeMeleeDrainReach(impact)) return DomainStatus::Applied;
            if(!execution.reactionPrepared) {
                DomainStatus status=DomainStatus::Applied;
                if(execution.defense.block) status=target.monster?ports_.monsters.block(target.id(),tick.tick).status:
                    ports_.effects.block({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick}).status;
                else if(target.player && execution.defense.avoidance!=WeaponAvoidance::None)
                    status=ports_.effects.avoidance({target.player->player,target.id(),target.player->area,ports_.areas.at(target.player->area).generation,0,tick.tick},execution.defense.avoidance,source.id()).status;
                if(status==DomainStatus::Capacity) return status;
                execution.reactionPrepared=true;
            }
            return finishNativeMeleeDrain(impact,0,tick);
        }
        impact.unblockable=true; // GetResultFlags already consumed the defender's block/dodge stream.
    }
    if(execution.progressive && !execution.overlayPrepared) {
        std::vector<DomainFact> facts;
        for(const auto &charge:ports_.effects.progressiveCharges(source.id())) if(charge.program->progressiveDamage==1 && charge.program->targetOverlay>0)
            facts.emplace_back(OverlayFact{target.id(),uint8_t(target.player?0:1),impact.area,charge.program->targetOverlay});
        if(!facts.empty()) {const auto result=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},std::move(facts)});if(!result) return result.status;}
        execution.overlayPrepared=true;
    }
    if(!execution.rolled) {
        const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
        auto weapon=impact.weapon;
        const auto damage=evaluate(random,[&](SkillFormulaValues &values) {
            if(!execution.melee) return rollNativeSkillDamage(values,execution.skill,execution.rank,random);
            auto skill=weapon->wearSkill;
            if(!execution.kick) skill.weapon->damagePercent=nativeAdd(skill.weapon->damagePercent,typeBonus("damage_vs_montype"));
            if(execution.progressive) skill.weapon->damagePercent=nativeAdd(skill.weapon->damagePercent,ports_.effects.progressivePhysicalPercent(source.id()));
            const auto sourceDamage=execution.sourceDamage.value_or(catalog.skills.at(skill.sourceId).number("srcdam"));
            const auto base=execution.initialDamage?*execution.initialDamage:MissileHitDamage{};
            const auto stats=sourceStats();if(!stats) throw std::logic_error("Missing native melee source stat view");
            const auto modifiers=source.player?source.player->totals.character.combat:ports_.effects.combatModifiers(source.id(),tick.tick);
            auto equipment=base;
            if(skill.druid && (skill.druid->program==DruidProgram::Rabies || skill.druid->program==DruidProgram::BearSmite)) {
                rollNativeSkillElement(equipment,values,execution.skill,execution.rank,random);
                if(skill.druid->program==DruidProgram::Rabies) equipment.poisonLength=std::max(equipment.poisonLength,10);
            }
            if(execution.kick) {
                const auto unmerged=ports_.effects.unitStatValues(source.id(),tick.tick,EntityId{});
                if(!unmerged) throw std::logic_error("Missing native kick unmerged weapon view");
                const auto stat=[&](std::string_view name){return unmerged->get({programs.stats->names.at(std::string(name)),0});};
                const auto *gear=source.player?&source.player->totals.equipment:source.monster->equipmentStats?&*source.monster->equipmentStats:nullptr;
                const auto boots=gear?gear->kickBoots:EquipmentStats::KickBoots{};
                AssassinKickInput input{stat("strength"),stat("dexterity"),skill.weapon->damagePercent,stat("item_kickdamage"),
                    boots.minimum,boots.maximum,boots.strengthBonus,boots.dexterityBonus,stat("damagepercent"),stat("item_maxdamage_percent"),boots.equipped};
                const auto [low,high]=assassinKickRange(input);
                equipment.channels[0]=nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeSubtract(high,low)))));
                rollNativeSkillElement(equipment,values,execution.skill,execution.rank,random);
                fillNativeWeaponElements(equipment,*unmerged,*programs.stats,128,random,source.monster && !source.monster->hireling);
                weapon->nativeFixedPhysical=true;weapon->physicalFlat=0;weapon->critical=false;
                equipment.knockback=execution.kickKnockback;
                impact.areaDamage->hitFlags=0x23;
                if(execution.kickKnockback) impact.areaDamage->resultFlags|=0xc;
            } else {
                *weapon=rollNativeWeaponPhysical(weapon->weapon,modifiers,skill,weapon->level,
                    target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead,random,{sourceDamage,base.channels[0]});
                equipment.channels[0]=nativeChecked(weapon->channels[0]);
                fillNativeWeaponElements(equipment,*stats,*programs.stats,sourceDamage,random,source.monster && !source.monster->hireling);
            }
            if(source.monster && source.monster->rule.criticalChance>0) {
                execution.monsterCritical=int(rollRandom(random)%100)<source.monster->rule.criticalChance;
                if(execution.monsterCritical) for(auto &channel:equipment.channels) channel=nativeMultiply(channel,2);
            }
            for(size_t channel=0;channel<equipment.channels.size();++channel) weapon->channels[channel]=equipment.channels[channel];
            weapon->coldFrames=equipment.coldLength;weapon->poisonFrames=equipment.poisonLength;weapon->stunFrames=equipment.stunLength;
            weapon->lifeLeech=equipment.lifeLeech;weapon->manaLeech=equipment.manaLeech;
            weapon->staminaLeech=equipment.staminaLeech;
            if(equipment.hitClass) weapon->hitClass=equipment.hitClass;
            MissileHitDamage damage=equipment;
            damage.lifeLeech=weapon->lifeLeech;damage.manaLeech=weapon->manaLeech;damage.coldLength=weapon->coldFrames;
            damage.staminaLeech=weapon->staminaLeech;
            damage.freezeLength=weapon->freezeFrames;damage.poisonLength=weapon->poisonFrames;damage.softHit=weapon->softHit;
            damage.stunLength=weapon->stunFrames;damage.critical=weapon->critical;damage.hitClass=uint8_t(weapon->hitClass>=0?weapon->hitClass:weapon->weapon.hitClass);
            damage.knockback=equipment.knockback;
            if(execution.kick && skill.assassin && skill.assassin->program==AssassinProgram::DragonTalon) damage.hitClass=1;
            damage.bypassUndead=equipment.bypassUndead;damage.bypassDemon=equipment.bypassDemon;damage.bypassBeasts=equipment.bypassBeasts;return damage;
        });
        const auto committed=consume(expected,random);if(committed!=DomainStatus::Applied) return committed;
        impact.areaDamage->rolled->value=damage;impact.weapon=std::move(weapon);execution.rolled=true;
    }
    if(execution.melee && !execution.fillEventComplete) {
        if(target.player || (target.monster && target.monster->hireling)) {
            if(!execution.attempt) execution.attempt=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareAttackEvents(source.id(),target.id(),tick.tick));
            const auto result=ports_.effects.advanceDamageEvents(*execution.attempt,tick.tick);if(!result) return result.status;
            execution.attempt.reset();
        }
        execution.fillEventComplete=true;
    }
    if(execution.monsterCritical && !execution.monsterCriticalPosted) {
        auto &damage=*impact.areaDamage->rolled->value;
        if(!(damage.hitClass&0xf0)) {
            damage.hitClass|=0x10;if(impact.weapon) impact.weapon->hitClass=damage.hitClass;
        } else {
            const auto posted=ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},impact.area},
                {OverlayFact{target.id(),uint8_t(target.player?0:1),impact.area,54}}});
            if(!posted) return posted.status;
        }
        execution.monsterCriticalPosted=true;
    }
    if(execution.melee && !execution.elementsComplete) {
        const auto expected=caster->random();auto random=expected;auto weapon=*impact.weapon;
        if(execution.progressive) ports_.effects.progressiveDamage(source.id(),weapon,random);
        auto damage=*impact.areaDamage->rolled->value;
        const auto channels=targetWeaponChannels(weapon,target.monster && target.monster->rule.demon,target.monster && target.monster->rule.undead);
        for(size_t channel=0;channel<channels.size();++channel) damage.channels[channel]=nativeChecked(channels[channel]);
        damage.lifeLeech=weapon.lifeLeech;damage.manaLeech=weapon.manaLeech;damage.staminaLeech=weapon.staminaLeech;
        damage.coldLength=weapon.coldFrames;damage.poisonLength=weapon.poisonFrames;damage.freezeLength=weapon.freezeFrames;
        if(weapon.wearSkill.druid) {
            const auto program=weapon.wearSkill.druid->program;
            if(program==DruidProgram::FireClaws || program==DruidProgram::Hunger) {
                evaluate(random,[&](SkillFormulaValues &values) {
                    rollNativeSkillElement(damage,values,execution.skill,execution.rank,random);
                    if(program==DruidProgram::Hunger) {
                        const auto calculation=[&](std::string_view field) {const auto value=values.calculation(FormulaDomain::Skill,execution.skill,execution.rank,field);
                            if(!value) throw std::logic_error("Missing native Hunger formula");return *value;};
                        damage.channels[0]=nativeAdd(damage.channels[0],nativePercentage(damage.channels[0],calculation("calc1"),100));
                        damage.lifeLeech=nativeAdd(damage.lifeLeech,calculation("calc2"));
                        damage.manaLeech=nativeAdd(damage.manaLeech,calculation("calc3"));
                    }
                    return 0;
                });
                for(size_t channel=0;channel<damage.channels.size();++channel) weapon.channels[channel]=damage.channels[channel];
                weapon.lifeLeech=damage.lifeLeech;weapon.manaLeech=damage.manaLeech;
                weapon.coldFrames=damage.coldLength;weapon.poisonFrames=damage.poisonLength;
            }
        }
        if(weapon.hitClass>=0) damage.hitClass=uint8_t(weapon.hitClass);
        const auto result=consume(expected,random);if(result!=DomainStatus::Applied) return result;
        impact.weapon=std::move(weapon);impact.areaDamage->rolled->value=std::move(damage);execution.elementsComplete=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::postNativeSkillCharge(SpellImpact &impact,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee || impact.nativeSkill->chargePosted) return DomainStatus::Applied;
    auto &execution=*impact.nativeSkill;
    const auto caster=skills::nativeCaster(ports_.players,ports_.monsters,impact.source);if(!caster) return DomainStatus::Stale;
    if(execution.hit) {
        const auto &skill=impact.weapon->wearSkill;
        if(skill.assassin && skill.assassin->chargeStat>=0) {
            const auto result=ports_.effects.progressiveCharge(caster->context(ports_.areas,tick.tick),skill);
            if(!result) return result.status;
        }
    }
    execution.chargePosted=true;
    return DomainStatus::Applied;
}
bool System::nativeMeleeDrainReach(SpellImpact &impact) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee || impact.nativeDrainHeld || impact.nativeSkill->drainRangeChecked) return true;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(impact.source),target=participants.find(impact.targets.at(impact.next));
    if(!source.id() || !target.id()) return false;
    const auto sourceArea=source.player?source.player->area:source.monster->area,targetArea=target.player?target.player->area:target.monster->area;
    const auto &area=ports_.areas.at(sourceArea);
    const auto point=target.position()+ports_.areas.at(targetArea).definition.origin-area.definition.origin;
    if(!unitMeleeReach(area.definition.collision,source.position(),source.size(),point,target.size(),
        source.player?source.player->totals.equipment.weapons[0].rangeAdder:source.monster->rule.meleeRange,source.player?1:3)) return false;
    impact.nativeSkill->drainRangeChecked=true;return true;
}
DomainStatus System::finishNativeMeleeDrain(SpellImpact &impact,int64_t physical,TickContext tick) {
    if(!impact.nativeSkill || !impact.nativeSkill->melee) return DomainStatus::Applied;
    auto &execution=*impact.nativeSkill;
    if(!execution.attackItemsComplete) {
        if(const auto *player=ports_.players.findActor(impact.source)) {
            if(!execution.attackItems) {
                const auto target=Participants{ports_.players,ports_.monsters,ports_.social}.find(impact.targets.at(impact.next));
                const ActorContext actor{player->player,player->actor,player->area,ports_.areas.at(player->area).generation,0,tick.tick};
                auto events=ports_.itemTriggers.prepare(actor,{ItemSkillEvent::Attack},target.id(),target.position());
                if(!events) return events.status;
                execution.attackItems=std::make_shared<reactions::ItemEventPlan>(std::move(*events.value));
            }
            if(ports_.itemTriggers.advance(*execution.attackItems,tick.tick)==StepStatus::Blocked) return DomainStatus::Capacity;
        }
        execution.attackItemsComplete=true;
    }
    if(!execution.drainEventsComplete) {
        if(!execution.drainEvents) execution.drainEvents=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareMeleeDrainEvents(impact.source,impact.targets.at(impact.next),tick.tick));
        const auto result=ports_.effects.advanceDamageEvents(*execution.drainEvents,tick.tick);if(!result) return result.status;
        execution.drainEventsComplete=true;
    }
    if(!execution.thornsComplete) {
        if(!execution.thornsEvents) execution.thornsEvents=std::make_shared<effects::NativeContactBatch>(ports_.effects.prepareThornsEvents(impact.source,impact.targets.at(impact.next),physical,tick.tick));
        const auto result=ports_.effects.advanceDamageEvents(*execution.thornsEvents,tick.tick);if(!result) return result.status;
        execution.thornsComplete=true;
    }
    return DomainStatus::Applied;
}
DomainStatus System::finishNativeSkillFlags(SpellImpact &impact,TickContext tick) {
    if(!impact.nativeSkill || impact.nativeSkill->flagsComplete) return DomainStatus::Applied;
    const auto &execution=*impact.nativeSkill;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto source=participants.find(impact.source),target=participants.find(impact.targets[impact.next]);
    if(!source.id() || !target.id()) return DomainStatus::Stale;
    if(execution.psychicHammer) {
        const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
        const RuleRandom draw=[&](int32_t low,int32_t high)->RuleNumber {
            return low>=high?low:nativeAdd(low,int32_t(limitedRandom(random,uint32_t(nativeAdd(nativeSubtract(high,low),1)))));
        };
        const auto stats=source.player?std::optional{source.player->totals.nativeStats}:ports_.effects.unitStatValues(source.id(),tick.tick);
        if(!stats) return DomainStatus::Unavailable;
        auto input=source.player?skills::formulaInput(*source.player,draw):skills::formulaInput(*source.monster,*stats,draw);
        SkillFormulaValues values(*impact.missileDamage->formulas,input);
        const auto field=target.player || (target.monster && target.monster->hireling)?"calc4":
            target.monster && target.monster->rule.boss?"calc3":
            target.monster && nativeUniqueMonster(target.monster->identity.rank)?"calc2":"calc1";
        const auto chance=values.calculation(FormulaDomain::Skill,execution.skill,std::max(execution.rank,1),field);
        if(!chance) throw std::logic_error("Unavailable native Psychic Hammer knockback formula");
        const bool knockback=*chance>0 && rollRandom(random)%100<uint32_t(*chance);
        if(random!=expected) {
            const auto committed=source.monster?ports_.monsters.consumeRandom(source.id(),expected,random).status:
                ports_.transactions.consumeRandom({source.player->player,source.id(),source.player->area,ports_.areas.at(source.player->area).generation,0,tick.tick},expected,random).status;
            if(committed!=DomainStatus::Applied) return committed;
        }
        impact.knockback|=knockback;
    }
    impact.nativeSkill->flagsComplete=true;return DomainStatus::Applied;
}
}
