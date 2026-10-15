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
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::resolvePlayerImpact(SpellImpact &impact,ParticipantView source,const PlayerState &owner,
    const PlayerState &target,uint64_t generation,TickContext tick) {
    if(impact.playerContact && impact.playerContact->targetIndex==impact.next) return finishPlayerContact(impact,target,tick);
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    if(!ParticipantView{&target,nullptr}.aliveIn(impact.area) || !participants.canHarm(source,{&target,nullptr}) ||
       impact.undeadOnly || !owner.rules.skills || (impact.nextDelay && !ports_.effects.missileHitAllowed(target.actor,tick.tick))) return DomainStatus::InvalidActor;
    if(!ports_.events.hasCapacity(12,4) || !ports_.effects.reactionCapacity()) return DomainStatus::Capacity;
    if(!impact.targetRandom || impact.plannedTarget!=impact.next) {
        impact.plannedTarget=impact.next;
        impact.targetRandom=impact.contactRandom?childRandom(*impact.contactRandom):childRandom(ports_.random);
        impact.wearApplied=false;impact.frenzyApplied=false;
    }
    auto random=*impact.targetRandom;
    const ActorContext victim{target.player,target.actor,target.area,generation,0,tick.tick};
    const ActorContext attacker{owner.player,owner.actor,owner.area,ports_.areas.at(owner.area).generation,0,tick.tick};
    const bool running=target.moving && target.runningNow;
    const auto miss=[&](DomainStatus status=DomainStatus::Applied) {
        if(status==DomainStatus::Applied && !impact.reaction && (impact.melee || (impact.weapon && !impact.weapon->projectile))) {impact.pendingAttempts=1;impact.attemptOnly=true;}
        return status;
    };
    if(impact.weapon && !(impact.weaponHit.has_value()?*impact.weaponHit:weaponContact(*impact.weapon,target.actor,tick.tick,random))) return miss();
    if(impact.monsterToHit && !running && limitedRandom(random,100)>=unsigned(physicalHitChance(impact.monsterLevel,impact.monsterRating,target.persistent.player.level,target.totals.character.defense))) return miss();
    const auto targetDamage=impact.currentDamage();
    const bool physicalMissile=impact.weapon || (impact.monsterHit?impact.monsterHit->channels[0]>0:impact.type==DamageType::Physical && targetDamage>0);
    if(!impact.unblockable && physicalMissile && !(impact.weapon && impact.weapon->smite) && target.totals.equipment.shield &&
       limitedRandom(random,100)<unsigned(target.totals.equipment.blockChance/(running?3:1))) return miss(ports_.effects.block(victim).status);
    const bool missile=!impact.melee && (!impact.weapon || impact.weapon->projectile);
    const auto avoided=impact.unblockable?WeaponAvoidance::None:rollWeaponAvoidance(target.totals.character.combat,target.moving,missile,random);
    if(avoided!=WeaponAvoidance::None) return miss(ports_.effects.avoidance(victim,avoided,impact.source).status);
    MonsterHit hit=impact.monsterHit.value_or(MonsterHit{});
    hit.softHit=hit.softHit || impact.softHit || impact.staticPercent!=0;
    hit.coldFrames=std::max(hit.coldFrames,impact.coldFrames);hit.poisonFrames=std::max(hit.poisonFrames,impact.poisonFrames);hit.stunFrames=std::max(hit.stunFrames,impact.stunFrames);
    hit.knockback=hit.knockback || impact.knockback;hit.hitClass=impact.hitClass;
    if(impact.weapon) {
        hit.channels=targetWeaponChannels(*impact.weapon,false,false);
        hit.poisonFrames=uint64_t(std::max(0,impact.weapon->poisonFrames));
        hit.stunFrames=uint64_t(std::max(0,impact.weapon->stunFrames));
        hit.hitClass=impact.weapon->weapon.hitClass;hit.critical=impact.weapon->critical;
        hit.knockback=hit.knockback || impact.weapon->knockback;
    } else if(!impact.monsterHit) hit.channels[size_t(impact.type)]=targetDamage;
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
    WeaponHitResult extras;std::optional<WeaponSkillDamage> leech;
    const auto &rules=*owner.rules.skills;
    if(impact.weapon) {
        leech=*impact.weapon;leech->lifeLeech=nativePercentage(leech->lifeLeech,percent,100);leech->manaLeech=nativePercentage(leech->manaLeech,percent,100);
        extras=calculateWeaponHitEffects(*leech,{int64_t(target.persistent.player.hp.raw()),target.totals.character.combat.physicalResist,100,1,false,false,false,true},0,{},random);
        adjustment.crushing=extras.crushing;
        if(extras.woundRate>0) adjustment.wound=OpenWoundsApplication{extras.woundRate,impact.source,impact.binding.credit,tick.tick+200,tick.tick+1};
        if(impact.weapon->wearSkill.weapon && impact.weapon->wearSkill.weapon->frenzy && !impact.frenzyApplied) {
            const auto result=ports_.effects.frenzy(attacker,impact.weapon->wearSkill);if(!result) return result.status;
            impact.frenzyApplied=true;
        }
        const bool wear=impact.weapon->wearChance>0 && limitedRandom(random,100)<unsigned(impact.weapon->wearChance);
        if(source.player && wear && !impact.wearApplied) {
            auto cost=ports_.inventory.weaponCost(attacker,impact.weapon->weapon,impact.weapon->wearSkill,false,false,unsigned(std::max(0,impact.weapon->wearAmount)));
            if(!cost) return cost.status;
            const auto committed=ports_.transactions.commit(std::move(*cost.value));if(!committed) return committed.status;
            impact.wearApplied=true;
        }
    }
    const bool whirlwind=impact.weapon && impact.weapon->wearSkill.weapon && impact.weapon->wearSkill.weapon->whirlwind;
    auto events=source.player && !whirlwind?ports_.itemTriggers.prepare(attacker,impact.weapon?std::initializer_list{ItemSkillEvent::Hit,ItemSkillEvent::Kill}:std::initializer_list{ItemSkillEvent::Kill},target.actor,target.position):DomainResult<reactions::ItemEventPlan>{DomainStatus::Applied,reactions::ItemEventPlan{}};
    if(!events) return events.status;
    const auto wear=impact.weapon && !impact.weapon->projectile?ports_.inventory.defensiveWear(target.player,random):std::nullopt;
    const auto remainingLife=std::max<int64_t>(0,int64_t(target.persistent.player.hp.raw())-extras.crushing);
    auto execution=std::make_shared<PlayerContactExecution>();execution->targetIndex=impact.next;
    execution->victim=victim;execution->attacker=attacker;execution->target=target.actor;
    execution->hit=hit;execution->states={rules.chilled,rules.poisoned,{}};execution->adjustment=adjustment;
    execution->wear=wear;execution->leech=std::move(leech);
    execution->extras=extras;execution->itemEvents=std::move(*events.value);
    execution->random=random;execution->hitClass=hitClass;execution->remainingLife=remainingLife;execution->missile=missile;
    impact.playerContact=std::move(execution);
    return finishPlayerContact(impact,target,tick);
}
DomainStatus System::advanceMeleeAttempts(SpellImpact &impact,TickContext tick) {
    while(impact.pendingAttempts) {
        if(!impact.attempt) impact.attempt=std::make_shared<effects::NativeContactBatch>(
            ports_.effects.prepareAttackEvents(impact.source,impact.targets.at(impact.next),tick.tick));
        const auto result=ports_.effects.advanceDamageEvents(*impact.attempt,tick.tick);
        if(!result) return result.status;
        --impact.pendingAttempts;impact.attempt.reset();
    }
    return DomainStatus::Applied;
}
DomainStatus System::finishPlayerContact(SpellImpact &impact,const PlayerState &target,TickContext tick) {
    auto &execution=*impact.playerContact;
    if(target.actor!=execution.target) return DomainStatus::Stale;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};const auto caster=participants.find(impact.source);
    const bool melee=!execution.missile && !impact.reaction;
    if(!execution.fillEventComplete) {
        if(melee && (caster.player || (caster.monster && caster.monster->hireling))) {
            if(!impact.pendingAttempts) impact.pendingAttempts=1;
            const auto attempted=advanceMeleeAttempts(impact,tick);if(attempted!=DomainStatus::Applied) return attempted;
        }
        execution.fillEventComplete=true;
    }
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
    if(!execution.meleeEventComplete) {
        if(melee) {
            if(!impact.pendingAttempts) impact.pendingAttempts=1;
            const auto attempted=advanceMeleeAttempts(impact,tick);if(attempted!=DomainStatus::Applied) return attempted;
        }
        execution.meleeEventComplete=true;
    }
    if(!execution.callbacksPrepared) {
        execution.callbacks=ports_.effects.prepareDamageEvents(impact.source,target.actor,execution.defense.damage.channels[0].dealt,
            execution.defense.damage.dealt,execution.missile,impact.returnFire || !execution.missile,tick.tick,impact.reaction);
        execution.callbacks.suppressAttacker=impact.suppressSourceEvents;
        execution.callbacksPrepared=true;
    }
    if(!execution.callbacksComplete) {
        const auto advanced=ports_.effects.advanceDamageEvents(execution.callbacks,tick.tick);if(!advanced) return advanced.status;
        execution.callbacksComplete=true;
    }
    auto reception=execution.defense.damage;reception.dealt=execution.callbacks.total;
    if(execution.nativeMissile) {
        const auto result=advanceMissileDrain(impact,{&target,nullptr},reception.channels[0].dealt,
            reception.drain.life,reception.drain.mana,reception.drain.stamina,tick);
        if(result!=DomainStatus::Applied) return result;
        reception.drain.mana=impact.missileContact->drain.manaField;
        reception.drain.stamina=impact.missileContact->drain.staminaField;
    }
    auto &random=execution.random;const auto &rules=*target.rules.skills;
    const auto received=ports_.effects.receiveMonster(execution.victim,impact.source,execution.hit,execution.states,execution.wear,
        std::ref(random),execution.adjustment,&reception,execution.nativeMissile);
    if(!received) return received.status;
    const auto hitClass=execution.hitClass;
    auto events=std::move(execution.itemEvents);const auto remainingLife=execution.remainingLife;
    auto leech=std::move(execution.leech);const auto extras=execution.extras;
    const auto source=participants.find(impact.source);
    const auto *owner=ports_.players.find(execution.attacker.player);
    if(execution.hostile && !impact.reaction) ports_.effects.triggerMonsterCurse(impact.source,tick.tick);
    if(execution.hostile && !execution.nativeMissile) {if(impact.contactRandom) impact.contactRandom=random;else ports_.random=random;}
    if(!execution.nativeMissile) state_.hitClassCursor=hitClass;
    ports_.itemTriggers.commit(std::move(events),target.persistent.player.hp<=0);
    if(impact.nextDelay) ports_.effects.missileHitDelay(target.actor,tick.tick+impact.nextDelay);
    if(leech) {
        leech->crushingBlow=leech->openWounds=0;
        const auto difficulty=size_t(owner?owner->persistent.difficulty:target.persistent.difficulty);
        const auto divisors=source.player?LeechDivisors{rules.lifeStealDivisor.at(difficulty),rules.manaStealDivisor.at(difficulty)}:LeechDivisors{};
        const auto gains=calculateWeaponHitEffects(*leech,{remainingLife,0,100,1,false,false,false,true},received.value->physical,divisors,random);
        impact.lifeGain=gains.life;impact.manaGain=gains.mana;
        impact.selfDamage=source.player?nativePercentage(nativeChecked(received.value->physical),leech->selfDamagePercent,100):0;
        if(gains.life && gains.mana) impact.leechOverlay=limitedRandom(random,2)?rules.manaStealOverlay:rules.lifeStealOverlay;
        else if(gains.life || gains.mana) impact.leechOverlay=gains.life?rules.lifeStealOverlay:rules.manaStealOverlay;
        if(extras.crushing>0) impact.contactOverlay=rules.crushingOverlay;
    }
    if(execution.nativeMissile && extras.crushing>0 && source.player && source.player->rules.skills)
        impact.contactOverlay=source.player->rules.skills->crushingOverlay;
    impact.conversion.reset(); // Conversion never changes a player's allegiance.
    impact.phase=ImpactPhase::FollowUp;impact.playerContact.reset();
    return DomainStatus::Applied;
}
DomainStatus System::finishMonsterContact(SpellImpact &impact,const monsters::Actor &target,TickContext tick) {
    auto &execution=*impact.monsterContact;
    if(target.id!=execution.target) return DomainStatus::Stale;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};const auto caster=participants.find(impact.source);
    const bool missile=!impact.melee && (!impact.weapon || impact.weapon->projectile),melee=!missile && !impact.reaction;
    if(!execution.fillEventComplete) {
        if(melee && (caster.player || (caster.monster && caster.monster->hireling))) {
            if(!impact.pendingAttempts) impact.pendingAttempts=1;
            const auto attempted=advanceMeleeAttempts(impact,tick);if(attempted!=DomainStatus::Applied) return attempted;
        }
        execution.fillEventComplete=true;
    }
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
    if(!execution.meleeEventComplete) {
        if(melee) {
            if(!impact.pendingAttempts) impact.pendingAttempts=1;
            const auto attempted=advanceMeleeAttempts(impact,tick);if(attempted!=DomainStatus::Applied) return attempted;
        }
        execution.meleeEventComplete=true;
    }
    if(!execution.callbacksPrepared) {
        execution.callbacks=ports_.effects.prepareDamageEvents(impact.source,execution.target,execution.physical,execution.amount,missile,impact.returnFire,tick.tick,impact.reaction);
        execution.callbacks.suppressAttacker=impact.suppressSourceEvents;
        execution.callbacksPrepared=true;
    }
    if(!execution.callbacksComplete) {
        const auto advanced=ports_.effects.advanceDamageEvents(execution.callbacks,tick.tick);
        if(!advanced) return advanced.status;
        execution.callbacksComplete=true;
    }
    const auto *live=ports_.monsters.find(execution.target);
    if(!live || live->area!=impact.area) return DomainStatus::Stale;
    if(execution.nativeMissile) {
        const auto result=advanceMissileDrain(impact,{nullptr,live},execution.physical,
            execution.lifeDrain,execution.manaDrain,execution.staminaDrain,tick);
        if(result!=DomainStatus::Applied) return result;
    }
    if(!execution.gainsPrepared) {
        if(execution.leech) {
            auto leech=*execution.leech;leech.crushingBlow=leech.openWounds=0;
            auto recipient=*live;recipient.life=std::max<int64_t>(0,live->life-execution.extras.crushing);
            const auto gains=weaponHitEffects(leech,recipient,impact.source,impact.binding.credit,0,
                std::min(execution.physical,recipient.life),execution.divisors,tick.tick,execution.random);
            execution.extras.healing=gains.healing;execution.extras.mana=gains.mana;
            const auto *owner=ports_.players.findActor(impact.source);
            if(owner && owner->rules.skills && execution.physical>0 && live->rule.drain>0) {
                const bool life=leech.lifeLeech>0,mana=leech.manaLeech>0;const auto &rules=*owner->rules.skills;
                if(life && mana) execution.leechOverlay=limitedRandom(execution.random,2)?rules.manaStealOverlay:rules.lifeStealOverlay;
                else if(life || mana) execution.leechOverlay=life?rules.lifeStealOverlay:rules.manaStealOverlay;
            }
        }
        if(impact.weapon) execution.recoil=nativePercentage(nativeChecked(std::max<int64_t>(0,std::min(
            std::max<int64_t>(0,live->life-execution.extras.crushing),execution.physical))),impact.weapon->selfDamagePercent,100);
        execution.gainsPrepared=true;
    }
    if(!execution.statuses.stunApplied) {
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
        execution.statuses.stunApplied=allowed && !live->rule.boss && live->rule.nativeVelocity!=0;
    }
    const auto poisonContact=execution.poison?std::max<int64_t>(0,execution.poison->rate):0;
    const auto direct=nativeSubtract(nativeChecked(execution.callbacks.total),nativeChecked(poisonContact));
    if(!execution.damageCommitted) {
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
    if(execution.nativeMissile) {
        const auto completed=finishMissileArmor(impact,participants.find(execution.target),tick);
        if(completed!=DomainStatus::Applied) return completed;
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
        if(!execution.nativeMissile) {if(impact.contactRandom) impact.contactRandom=execution.random;else ports_.random=execution.random;}
        if(impact.sourceHeal>0) ports_.monsters.heal(impact.source,impact.sourceHeal);
    }
    impact.lifeGain=execution.extras.healing;impact.manaGain=execution.extras.mana;impact.leechOverlay=execution.leechOverlay;
    if(source && source->hireling && !live->alive()) impact.lifeGain+=int64_t(source->petStats.attributes.combat.lifeOnKill)*256;
    const auto *owner=player?player:source && source->owner?ports_.players.find(*source->owner):nullptr;
    if(execution.extras.crushing>0 && owner && owner->rules.skills) impact.contactOverlay=owner->rules.skills->crushingOverlay;
    if(!execution.convert) impact.conversion.reset();
    impact.selfDamage=execution.recoil;impact.phase=ImpactPhase::FollowUp;
    impact.monsterContact.reset();
    return DomainStatus::Applied;
}
}
