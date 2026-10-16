#include "system.hpp"
#include "player_contact.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/inventory/system.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/weapon_hit.hpp"
#include "gameplay/combat/scaling.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "core/random.hpp"
#include "gameplay/skills/assassin_spec.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::resolvePlayerImpact(SpellImpact &impact,ParticipantView source,const PlayerState &owner,
    const PlayerState &target,uint64_t generation,TickContext tick) {
    if(impact.playerContact && impact.playerContact->targetIndex==impact.next) return finishPlayerContact(impact,target,tick);
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!ParticipantView{&target,nullptr}.aliveIn(impact.area) || !participants.canHarm(source,{&target,nullptr}) ||
       impact.undeadOnly || !owner.rules.skills || (impact.nextDelay && !ports_.effects.missileHitAllowed(target.actor,tick.tick))) return DomainStatus::InvalidActor;
    if(!ports_.events.hasCapacity(12,4) || !ports_.effects.reactionCapacity()) return DomainStatus::Capacity;
    const auto defended=directDefense(impact,source,{&target,nullptr},tick);
    if(!defended) return defended.status;
    if(!*defended.value) return DomainStatus::Applied;
    const ActorContext victim{target.player,target.actor,target.area,generation,0,tick.tick};
    const ActorContext attacker{owner.player,owner.actor,owner.area,ports_.areas.at(owner.area).generation,0,tick.tick};
    const bool missile=!impact.melee;
    const auto targetDamage=impact.currentDamage();
    MonsterHit hit=impact.monsterHit.value_or(MonsterHit{});
    hit.softHit=hit.softHit || impact.softHit || impact.staticPercent!=0;
    hit.coldFrames=std::max(hit.coldFrames,impact.coldFrames);hit.poisonFrames=std::max(hit.poisonFrames,impact.poisonFrames);hit.stunFrames=std::max(hit.stunFrames,impact.stunFrames);
    hit.knockback=hit.knockback || impact.knockback;hit.hitClass=impact.hitClass;
    if(!impact.monsterHit) hit.channels[size_t(impact.type)]=targetDamage;
    if(impact.staticPercent) {
        const auto amount=staticFieldDamageFixed(target.persistent.player.hp.raw(),target.totals.character.maxLifeFixed.raw(),
            impact.staticPercent,impact.staticFloors.at(size_t(target.persistent.difficulty)),nativeChecked(impact.minimumStaticDamage),rawResistance(target.totals.character,DamageType::Lightning));
        if(!amount) return DomainStatus::Applied;
        hit.channels[size_t(DamageType::Lightning)]=*amount;
    }
    // SUNITDMG_CalculateTotalDamage: 17%, before absorb/reduction/resistance;
    // duration channels do not receive this penalty. Other pets retain 100%.
    const int percent=damageClassPercentage({bool(source.player),source.monster && source.monster->hireling,
        source.monster && source.monster->revived(),source.monster && source.monster->rule.boss,
        source.monster && source.monster->rule.primeEvil},{true},100,source.id()==target.actor);
    if(!impact.damageScaled) scaleHitDamage(hit,percent);
    auto hitClass=state_.hitClassCursor;hit.hitClass=monsterDamageHitClass(hit,hit.hitClass,hitClass);
    effects::PlayerHitAdjustment adjustment;adjustment.credit=impact.binding.credit;
    adjustment.coldPierce=impact.coldPierce;adjustment.poisonPierce=impact.poisonPierce;
    const auto &rules=*owner.rules.skills;
    auto events=source.player?ports_.itemTriggers.prepare(attacker,{ItemSkillEvent::Kill},target.actor,target.position):
        DomainResult<reactions::ItemEventPlan>{DomainStatus::Applied,reactions::ItemEventPlan{}};
    if(!events) return events.status;
    const auto remainingLife=std::max<int64_t>(0,int64_t(target.persistent.player.hp.raw()));
    auto execution=std::make_shared<PlayerContactExecution>();execution->targetIndex=impact.next;
    execution->victim=victim;execution->attacker=attacker;execution->target=target.actor;
    execution->hit=hit;execution->states={rules.chilled,rules.poisoned,{}};execution->adjustment=adjustment;
    execution->itemEvents=std::move(*events.value);
    execution->hitClass=hitClass;execution->remainingLife=remainingLife;execution->missile=missile;
    impact.playerContact=std::move(execution);
    return finishPlayerContact(impact,target,tick);
}
DomainStatus System::finishPlayerContact(SpellImpact &impact,const PlayerState &target,TickContext tick) {
    auto &execution=*impact.playerContact;
    if(target.actor!=execution.target) return DomainStatus::Stale;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};const auto caster=participants.find(impact.source);
    const bool melee=!execution.missile && !impact.reaction;

    if(!execution.defensePrepared) {
        const auto prepared=ports_.effects.preparePlayerReception(execution.victim,impact.source,execution.hit,execution.adjustment);
        if(!prepared) return prepared.status;
        execution.defense=std::move(*prepared.value);execution.defensePrepared=true;
    }
    if(!execution.defenseCommitted) {
        auto publication=execution.defense;
        const auto committed=ports_.effects.commitPlayerReception(std::move(publication));if(!committed) return committed.status;
        execution.defenseCommitted=true;
    }
    {const auto allocated=postNativeMeleeAllocation(impact,tick);if(allocated!=DomainStatus::Applied) return allocated;}
    if(impact.nativeDrainHeld && impact.nativeSkill) {
        impact.nativeSkill->allocatedPhysical=nativeChecked(execution.defense.damage.channels[0].dealt);
        impact.nativeSkill->heldPrepared=true;return DomainStatus::Applied;
    }
    if(!nativeMeleeDrainReach(impact)) return DomainStatus::Unavailable;
    {const auto result=prepareMeleeDrainProcedures(impact,{&target,nullptr},tick);if(result!=DomainStatus::Applied) return result;}
    {const auto result=postNativeSkillCharge(impact,tick);if(result!=DomainStatus::Applied) return result;}
    if(impact.nativeSkill && impact.nativeSkill->progressive) {
        const auto result=advanceProgressive(impact,tick,execution.damageCommitted);if(!result) return result.status;
    }

    if(!execution.callbacksPrepared) {
        execution.callbacks=ports_.effects.prepareDamageEvents(impact.source,target.actor,execution.defense.damage.channels[0].dealt,
            execution.defense.damage.dealt,execution.missile,impact.returnFire || !execution.missile,tick.tick,impact.reaction);
        execution.callbacks.suppressAttacker=impact.suppressSourceEvents;
        if(impact.nativeSkill && impact.nativeSkill->melee) execution.callbacks.thorns=0;
        if(impact.nativeDamageFlagsOnly && impact.areaDamage && !(impact.areaDamage->hitFlags&0x20)) execution.callbacks.suppress=true;
        execution.callbacksPrepared=true;
    }
    if(!execution.callbacksComplete) {
        const auto advanced=ports_.effects.advanceDamageEvents(execution.callbacks,tick.tick);if(!advanced) return advanced.status;
        execution.callbacksComplete=true;
    }
    if(impact.nativeSkill && impact.nativeSkill->melee) {
        if(!impact.nativeSkill->physicalCapped) {
            auto &physical=execution.defense.damage.channels[0].dealt;
            physical=std::min(physical,std::max<int64_t>(0,int64_t(target.persistent.player.hp.raw())-execution.extras.crushing));
            impact.nativeSkill->physicalCapped=true;
        }
    }
    auto reception=execution.defense.damage;reception.dealt=execution.callbacks.total;
    if(execution.nativeMissile) {
        const auto result=advanceMissileDrain(impact,{&target,nullptr},reception.channels[0].dealt,
            reception.drain.life,reception.drain.mana,reception.drain.stamina,tick);
        if(result!=DomainStatus::Applied) return result;
        reception.drain.mana=impact.missileContact->drain.manaField;
        reception.drain.stamina=impact.missileContact->drain.staminaField;
    }
    const auto &rules=*target.rules.skills;
    if(!execution.damageCommitted) {
        // Held AllocCombat contacts do not reserve the defender's future
        // recovery seed. Earlier contacts and callbacks may have consumed it.
        const auto received=ports_.effects.receiveMonster(execution.victim,impact.source,execution.hit,execution.states,execution.wear,
            execution.adjustment,&reception,bool(impact.nativeSkill));
        if(!received) return received.status;
        execution.receivedPhysical=received.value->physical;execution.damageCommitted=true;
    }
    // DrainItemDurability calls ExecuteEvents (including drain, HP and states)
    // before durability, melee events and thorns. ExecuteMissileDamage only
    // chooses the final reaction/death mode afterwards.
    if(impact.nativeSkill && impact.nativeSkill->melee) {
        const auto armor=finishMissileArmor(impact,{&target,nullptr},tick);if(armor!=DomainStatus::Applied) return armor;
        const auto drained=finishNativeMeleeDrain(impact,execution.defense.damage.channels[0].dealt,tick);if(drained!=DomainStatus::Applied) return drained;
    }
    if(impact.nativeSkill) {
        const auto result=finishNativeSkillFlags(impact,tick);if(result!=DomainStatus::Applied) return result;
        if(impact.nativeSkill->psychicHammer && !impact.nativeSkill->reactionPrepared) {
            auto hit=execution.hit;hit.knockback|=impact.knockback;
            const auto reaction=ports_.effects.nativeMeleeReaction(target.actor,impact.source,hit,execution.callbacks.total,tick.tick);
            if(!reaction) return reaction.status;impact.nativeSkill->reactionPrepared=true;
        }
    }
    if(impact.nativeSkill && impact.nativeSkill->melee) {
        if(!impact.nativeSkill->actionComplete) {
            const auto action=impact.nativeSkill->defense.block?
                ports_.effects.block({target.player,target.actor,target.area,ports_.areas.at(target.area).generation,0,tick.tick}):
                ports_.effects.nativeMeleeReaction(target.actor,impact.source,execution.hit,execution.callbacks.total,tick.tick);
            if(!action) return action.status;impact.nativeSkill->actionComplete=true;
        }
    }
    if(impact.nativeSkill && impact.nativeSkill->progressive) {
        const auto result=advanceProgressive(impact,tick,true);if(!result) return result.status;
    }
    const auto hitClass=execution.hitClass;
    auto events=std::move(execution.itemEvents);const auto remainingLife=execution.remainingLife;
    const auto extras=execution.extras;
    const auto source=participants.find(impact.source);
    const auto *owner=ports_.players.find(execution.attacker.player);
    if(execution.hostile && !impact.reaction) ports_.effects.triggerMonsterCurse(impact.source,tick.tick);
    if(!execution.nativeMissile) state_.hitClassCursor=hitClass;
    ports_.itemTriggers.commit(std::move(events),target.persistent.player.hp<=0);
    if(impact.nextDelay) ports_.effects.missileHitDelay(target.actor,tick.tick+impact.nextDelay);
    impact.phase=ImpactPhase::FollowUp;impact.playerContact.reset();
    return DomainStatus::Applied;
}
DomainStatus System::finishMonsterContact(SpellImpact &impact,const monsters::Actor &target,TickContext tick) {
    auto &execution=*impact.monsterContact;
    if(target.id!=execution.target) return DomainStatus::Stale;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};const auto caster=participants.find(impact.source);
    const bool missile=!impact.melee && (!impact.weapon || impact.weapon->projectile),melee=!missile && !impact.reaction;

    if(!execution.receptionPrepared) {
        const bool directDrain=!execution.nativeMissile || (caster.monster && !caster.monster->hireling);
        MonsterHit hit;hit.channels=execution.channels;hit.lifeDrain=execution.lifeDrain;hit.mana=execution.manaDrain;hit.stamina=execution.staminaDrain;
        auto prepared=ports_.effects.prepareUnitReception(execution.target,impact.source,hit,tick.tick,execution.pierce,execution.bypass,execution.sanctuary,directDrain);
        if(!prepared) return prepared.status;
        execution.defense=std::move(*prepared.value);const auto &received=execution.defense->damage;
        execution.amount=received.dealt;execution.absorbed=received.absorbed;
        execution.physical=received.channels[0].dealt;if(execution.poison) execution.poison->rate=received.channels[5].dealt;
        execution.lifeDrain=received.drain.life;execution.manaDrain=received.drain.mana;execution.staminaDrain=received.drain.stamina;
        execution.receptionPrepared=true;
    }
    if(!execution.defenseCommitted) {
        const auto committed=ports_.effects.commitUnitReception(*execution.defense);if(!committed) return committed.status;
        execution.defenseCommitted=true;
    }
    {const auto allocated=postNativeMeleeAllocation(impact,tick);if(allocated!=DomainStatus::Applied) return allocated;}
    if(impact.nativeDrainHeld && impact.nativeSkill) {
        impact.nativeSkill->allocatedPhysical=nativeChecked(execution.physical);
        impact.nativeSkill->heldPrepared=true;return DomainStatus::Applied;
    }
    if(!nativeMeleeDrainReach(impact)) return DomainStatus::Unavailable;
    {const auto result=prepareMeleeDrainProcedures(impact,{nullptr,&target},tick);if(result!=DomainStatus::Applied) return result;}
    {const auto result=postNativeSkillCharge(impact,tick);if(result!=DomainStatus::Applied) return result;}
    if(impact.nativeSkill && impact.nativeSkill->progressive) {
        const auto result=advanceProgressive(impact,tick,execution.damageCommitted);if(!result) return result.status;
    }

    if(!execution.callbacksPrepared) {
        execution.callbacks=ports_.effects.prepareDamageEvents(impact.source,execution.target,execution.physical,execution.amount,missile,impact.returnFire,tick.tick,impact.reaction);
        execution.callbacks.suppressAttacker=impact.suppressSourceEvents;
        if(impact.nativeSkill && impact.nativeSkill->melee) execution.callbacks.thorns=0;
        if(impact.nativeDamageFlagsOnly && impact.areaDamage && !(impact.areaDamage->hitFlags&0x20)) execution.callbacks.suppress=true;
        execution.callbacksPrepared=true;
    }
    if(!execution.callbacksComplete) {
        const auto advanced=ports_.effects.advanceDamageEvents(execution.callbacks,tick.tick);
        if(!advanced) return advanced.status;
        execution.callbacksComplete=true;
    }
    if(impact.nativeSkill && impact.nativeSkill->melee) {
        if(!impact.nativeSkill->physicalCapped) {
            execution.physical=std::min(execution.physical,std::max<int64_t>(0,target.life-execution.extras.crushing));
            impact.nativeSkill->physicalCapped=true;
        }
    }
    const auto *live=ports_.monsters.find(execution.target);
    if(!live || live->area!=impact.area) return DomainStatus::Stale;
    if(execution.nativeMissile) {
        const auto result=advanceMissileDrain(impact,{nullptr,live},execution.physical,
            execution.lifeDrain,execution.manaDrain,execution.staminaDrain,tick);
        if(result!=DomainStatus::Applied) return result;
    }
    if(!execution.stunPrepared) {
        bool allowed=execution.stun>0;
        const bool unique=nativeUniqueMonster(live->identity.rank);
        if(allowed && unique) {
            const auto source=participants.find(impact.source);
            if(!source.id()) allowed=false;
            else {
                const auto expected=source.player?source.player->combatRandom:source.monster->combatRandom;auto random=expected;
                allowed=limitedRandom(random,100)>=90;
                DomainResult<> consumed;
                if(source.monster) consumed=ports_.monsters.consumeRandom(source.id(),expected,random);
                else {
                    const auto &owner=*source.player;const auto &area=ports_.areas.at(owner.area);
                    consumed=ports_.transactions.consumeRandom({owner.player,owner.actor,owner.area,area.generation,0,tick.tick},expected,random);
                }
                if(!consumed) return consumed.status;
            }
        }
        execution.statuses.stunApplied=allowed && !live->rule.boss && live->rule.nativeVelocity!=0;execution.stunPrepared=true;
    }
    const auto poisonContact=execution.poison?std::max<int64_t>(0,execution.poison->rate):0;
    const auto direct=nativeSubtract(nativeChecked(execution.callbacks.total),nativeChecked(poisonContact));
    if(!execution.damageCommitted) {
        execution.statuses.deferAction=bool(impact.nativeSkill);
        const auto result=ports_.monsters.damage(live->id,impact.binding.credit,direct,tick.tick,
            execution.cold,impact.freeze,execution.hitClass,execution.poison,
            impact.type==DamageType::Poison && !impact.weapon,execution.stun,execution.extras.wound,execution.absorbed,
            execution.clearStun,execution.extras.crushing,execution.pierce[4],impact.softHit || impact.staticPercent!=0,
            execution.nativeMissile?std::optional{execution.freeze}:std::nullopt,execution.getHit,
            execution.statuses);
        if(!result) return result.status;
        if(execution.nativeMissile) ports_.monsters.drainResources(execution.target,
            impact.missileContact->drain.manaField,impact.missileContact->drain.staminaField,tick.tick);
        execution.damageCommitted=true;
    }
    if(impact.nativeSkill && impact.nativeSkill->melee) {
        const auto armor=finishMissileArmor(impact,participants.find(execution.target),tick);if(armor!=DomainStatus::Applied) return armor;
        const auto drained=finishNativeMeleeDrain(impact,execution.physical,tick);if(drained!=DomainStatus::Applied) return drained;
    }
    if(impact.nativeSkill) {const auto result=finishNativeSkillFlags(impact,tick);if(result!=DomainStatus::Applied) return result;}
    if(execution.nativeMissile && !(impact.nativeSkill && impact.nativeSkill->melee)) {
        const auto completed=finishMissileArmor(impact,participants.find(execution.target),tick);
        if(completed!=DomainStatus::Applied) return completed;
    }
    if(impact.nativeSkill && !impact.nativeSkill->actionComplete) {
        const auto action=impact.nativeSkill->defense.block && live->life>0 && !impact.knockback?
            ports_.monsters.block(execution.target,tick.tick):
            ports_.monsters.finishDamageAction(execution.target,impact.binding.credit,execution.callbacks.total,tick.tick,execution.hitClass,
                impact.softHit || impact.staticPercent!=0,execution.getHit);
        if(!action) return action.status;impact.nativeSkill->actionComplete=true;
    }
    if(impact.nativeSkill && impact.nativeSkill->progressive) {
        const auto result=advanceProgressive(impact,tick,true);if(!result) return result.status;
    }
    if(execution.hostile && !impact.reaction) ports_.effects.triggerMonsterCurse(impact.source,tick.tick);
    const auto *source=ports_.monsters.find(impact.source);
    const auto *player=ports_.players.findActor(impact.source);
    if(source && source->hireling) ports_.monsters.recordHirelingKill(live->id,source->id);
    ports_.itemTriggers.commit(std::move(execution.itemEvents),!live->alive());
    if(impact.nextDelay) ports_.monsters.hitDelay(live->id,tick.tick+impact.nextDelay);
    if(impact.knockback && live->life>0) {
        if(source || player) ports_.monsters.knockback(live->id,source?source->position:player->position,tick.tick);
        cancel(live->id);
    }
    if(execution.hostile) {
        if(impact.sourceHeal>0) ports_.monsters.heal(impact.source,impact.sourceHeal);
    }
    impact.lifeGain=execution.extras.healing;impact.manaGain=execution.extras.mana;impact.leechOverlay=execution.leechOverlay;
    if(source && source->hireling && !live->alive()) impact.lifeGain+=int64_t(source->petStats.attributes.combat.lifeOnKill)*256;
    const auto *owner=player?player:source && source->owner?ports_.players.find(*source->owner):nullptr;
    if(execution.extras.crushing>0 && owner && owner->rules.skills) impact.contactOverlay=owner->rules.skills->crushingOverlay;
    impact.phase=ImpactPhase::FollowUp;
    impact.monsterContact.reset();
    return DomainStatus::Applied;
}
}
