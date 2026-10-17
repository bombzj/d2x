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
        impact.environmentDefense.reset();impact.environmentDefenseCommitted=false;impact.playerContact.reset();++impact.next;
    };
    while(impact.next<impact.targets.size()) {
        const auto target=participants.find(impact.targets[impact.next]);
        if(!target.aliveIn(impact.area) || (target.monster && !target.monster->damageable())) {nextTarget();continue;}
        const bool retained=(impact.environmentDefense && impact.environmentDefense->target==target.id()) ||
            (impact.playerContact && impact.playerContact->target==target.id());
        if(!retained && impact.nextDelay && (target.player?!ports_.effects.missileHitAllowed(target.id(),tick.tick):target.monster->nextHitTick>tick.tick)) {nextTarget();continue;}
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
        if(target.player) {
            const auto &player=*target.player;
            const ActorContext actor{player.player,player.actor,player.area,area->generation,0,tick.tick};
            if(impact.directLife) result=ports_.transactions.resources(actor,player.characterRevision,
                Fixed8::fromRaw(std::max<int64_t>(256,int64_t(player.persistent.player.hp.raw())-hit.channels[size_t(impact.type)])),player.persistent.player.mana,player.persistent.player.stamina);
            else {
                if(impact.nativeObjectDamage) {
                    if(!impact.playerContact) {
                        auto contact=std::make_shared<PlayerContactExecution>();contact->victim=actor;
                        contact->target=target.id();contact->hit=hit;contact->hitClass=cursor;
                        impact.playerContact=std::move(contact);
                        state_.hitClassCursor=cursor;
                    }
                    auto &contact=*impact.playerContact;
                    if(!contact.defensePrepared) {
                        auto prepared=ports_.effects.preparePlayerReception(actor,impact.source,contact.hit);
                        if(!prepared) return prepared.status;
                        contact.defense=std::move(*prepared.value);contact.defensePrepared=true;
                    }
                    if(!contact.defenseCommitted) {
                        auto candidate=contact.defense;
                        const auto committed=ports_.effects.commitPlayerReception(std::move(candidate));
                        if(!committed) return committed.status;
                        contact.defenseCommitted=true;
                    }
                    if(!contact.callbacksPrepared) {
                        contact.callbacks=ports_.effects.prepareDamageEvents(impact.source,target.id(),contact.defense.damage.channels[0].dealt,
                            contact.defense.damage.dealt,false,false,tick.tick);
                        contact.callbacks.thorns=0;
                        contact.callbacksPrepared=true;
                    }
                    if(!contact.callbacksComplete) {
                        const auto advanced=ports_.effects.advanceDamageEvents(contact.callbacks,tick.tick);
                        if(!advanced) return advanced.status;
                        contact.callbacksComplete=true;
                    }
                    auto reception=contact.defense.damage;reception.dealt=contact.callbacks.total;
                    const auto received=ports_.effects.receiveMonster(actor,impact.source,contact.hit,impact.monsterStates,{}, {},&reception);
                    result={received.status,received?std::optional{std::monostate{}}:std::nullopt};
                    cursor=state_.hitClassCursor;
                } else {
                    const auto received=ports_.effects.receiveMonster(actor,impact.source,hit,impact.monsterStates);
                    result={received.status,received?std::optional{std::monostate{}}:std::nullopt};
                }
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
