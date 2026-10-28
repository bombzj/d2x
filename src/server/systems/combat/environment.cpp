#include "system.hpp"
#include "player_contact.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include "participants.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <algorithm>
#include "core/random.hpp"
#include "gameplay/combat/accuracy.hpp"
#include "gameplay/combat/avoidance.hpp"

namespace d2x::server::combat {
DomainStatus System::resolveEnvironment(SpellImpact &impact,TickContext tick) {
    const auto *area=ports_.areas.find(impact.area);
    if(!area || area->generation!=impact.environmentGeneration || area->definition.town) return DomainStatus::Stale;
    const Participants participants{ports_.players,ports_.monsters,ports_.social};
    const auto nextTarget=[&] {
        impact.environmentDefense.reset();impact.environmentDefenseCommitted=false;
        impact.playerContact.reset();impact.monsterContact.reset();impact.phase=ImpactPhase::Contact;++impact.next;
    };
    while(impact.next<impact.targets.size()) {
        const auto target=participants.find(impact.targets[impact.next]);
        const bool retained=(impact.environmentDefense && impact.environmentDefense->target==target.id()) ||
            (impact.playerContact && impact.playerContact->target==target.id()) ||
            (impact.monsterContact && impact.monsterContact->target==target.id());
        if((!target.player && !target.monster) || (!retained && (!target.aliveIn(impact.area) || (target.monster && !target.monster->damageable())))) {nextTarget();continue;}
        if(!retained && impact.nextDelay && (target.player?!ports_.effects.missileHitAllowed(target.id(),tick.tick):target.monster->nextHitTick>tick.tick)) {nextTarget();continue;}
        if(!impact.directLife) storeLastAttacker(impact,target,impact.source);
        if(!impact.directLife && impact.monsterHit && !retained) {
            const auto defended=directDefense(impact,participants.find(impact.source),target,tick);
            if(!defended) return defended.status;
            if(!*defended.value) {nextTarget();continue;}
        }
        MonsterHit hit=impact.monsterHit.value_or(MonsterHit{});
        auto cursor=state_.hitClassCursor;
        if(!impact.monsterHit) hit.channels[size_t(impact.type)]=impact.currentDamage();
        if(impact.playerContact) hit=impact.playerContact->hit;
        else if(!impact.directLife) hit.hitClass=monsterDamageHitClass(hit,impact.hitClass,cursor);
        DomainResult<> result;
        if(impact.nativeObjectDamage && !impact.directLife) {
            // Object damage also enters ExecuteEvents. Retain the same
            // pre-HP and Killed callback cursors as unit contacts, including
            // a lethal hit or a victim teleported before output can resume.
            if(!retained) {
                if(target.player) {
                    auto contact=std::make_shared<PlayerContactExecution>();
                    const auto &player=*target.player;
                    contact->victim={player.player,player.actor,player.area,area->generation,0,tick.tick};
                    contact->target=player.actor;contact->hit=hit;contact->hitClass=cursor;contact->states=impact.monsterStates;
                    impact.playerContact=std::move(contact);
                } else {
                    auto contact=std::make_shared<MonsterContactExecution>();
                    contact->target=target.id();contact->channels=hit.channels;contact->hitClass=hit.hitClass;
                    contact->cold=hit.coldFrames;contact->freeze=hit.freezeFrames;contact->stun=hit.stunFrames;
                    contact->getHit=hit.getHit;contact->statuses={impact.source,hit.burn,hit.burnFrames};
                    if(hit.channels[5]>0) contact->poison=PoisonApplication{hit.channels[5],hit.poisonFrames,impact.monsterStates.poison.id};
                    impact.monsterContact=std::move(contact);
                }
                state_.hitClassCursor=cursor;
            }
            const auto status=target.player?finishPlayerContact(impact,*target.player,tick):finishMonsterContact(impact,*target.monster,tick);
            if(isDeferred(status) || status==DomainStatus::Conflict) return status;
            if(status!=DomainStatus::Applied) return status;
            nextTarget();continue;
        }
        if(target.player) {
            const auto &player=*target.player;
            const ActorContext actor{player.player,player.actor,player.area,area->generation,0,tick.tick};
            if(impact.directLife) result=ports_.transactions.resources(actor,player.characterRevision,
                Fixed8::fromRaw(std::max<int64_t>(256,int64_t(player.persistent.player.hp.raw())-hit.channels[size_t(impact.type)])),player.persistent.player.mana,player.persistent.player.stamina);
            else {
                const auto received=ports_.effects.receiveMonster(actor,impact.source,hit,impact.monsterStates);
                result={received.status,received?std::optional{std::monostate{}}:std::nullopt};
            }
        } else {
            const auto &unit=*target.monster;int64_t amount=0,absorbed=0;
            if(!impact.directLife) {
                if(!retained) {
                    auto defense=ports_.effects.prepareUnitReception(unit.id,impact.source,hit,tick.tick);
                    if(!defense) return defense.status;
                    impact.environmentDefense=std::make_shared<effects::UnitReceptionPlan>(std::move(*defense.value));
                    impact.environmentDefenseCommitted=false;
                }
                if(!impact.environmentDefenseCommitted) {
                    const auto committed=ports_.effects.commitUnitReception(*impact.environmentDefense);if(!committed) return committed.status;
                    impact.environmentDefenseCommitted=true;
                }
                const auto &received=impact.environmentDefense->damage;
                for(size_t channel=0;channel<5;++channel) amount=nativeAdd(nativeChecked(amount),nativeChecked(received.channels[channel].dealt));
                absorbed=received.absorbed;
            } else {
                for(size_t channel=0;channel<5;++channel) amount=nativeAdd(nativeChecked(amount),nativeChecked(hit.channels[channel]));
            }
            std::optional<PoisonApplication> poison;
            if(hit.channels[5]>0) {
                const auto rate=impact.environmentDefense?impact.environmentDefense->damage.channels[5].dealt:
                    ports_.effects.unitDamageFixed(unit.id,hit.channels[5],DamageType::Poison,tick.tick).dealt;
                poison=PoisonApplication{rate,hit.poisonFrames,impact.monsterStates.poison.id};
            }
            const auto cold=hit.coldFrames;
            result=impact.directLife?ports_.monsters.reduceShrineLife(unit.id,amount):ports_.monsters.damage(unit.id,{},amount,tick.tick,cold,false,hit.hitClass,poison,impact.type==DamageType::Poison,0,{},absorbed);
        }
        if(isDeferred(result.status) || result.status==DomainStatus::Conflict ||
            result.status==DomainStatus::NotImplemented || result.status==DomainStatus::InvalidRequest) return result.status;
        if(result && impact.nextDelay) {if(target.player) ports_.effects.missileHitDelay(target.id(),tick.tick+impact.nextDelay);else ports_.monsters.hitDelay(target.id(),tick.tick+impact.nextDelay);}
        state_.hitClassCursor=cursor;nextTarget();
    }
    return DomainStatus::Applied;
}
}
