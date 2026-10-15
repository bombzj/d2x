#include "system.hpp"
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
    while(impact.next<impact.targets.size()) {
        const auto target=participants.find(impact.targets[impact.next]);
        if(!target.aliveIn(impact.area) || (target.monster && !target.monster->damageable())) {++impact.next;continue;}
        if(impact.nextDelay && (target.player?!ports_.effects.missileHitAllowed(target.id(),tick.tick):target.monster->nextHitTick>tick.tick)) {++impact.next;continue;}
        auto random=impact.contactRandom.value_or(initialRandom(uint32_t(impact.projectile.value)));const auto commitRandom=[&] {impact.contactRandom=random;};
        const bool retained=impact.environmentDefense && impact.environmentDefense->target==target.id();
        if(impact.monsterToHit && !retained) {
            const bool running=target.player && target.player->moving && target.player->runningNow;
            if(!running && int(limitedRandom(random,100))>=physicalHitChance(impact.monsterLevel,impact.monsterRating,target.player?target.player->persistent.player.level:target.monster->rule.level,target.player?target.player->totals.character.defense:ports_.effects.unitDefense(target.id(),tick.tick))) {commitRandom();++impact.next;continue;}
        }
        if(!impact.directLife && impact.monsterHit && !impact.unblockable && !retained) {
            if(!ports_.effects.reactionCapacity() || !ports_.events.hasCapacity(4,2)) return DomainStatus::Capacity;
            const auto *player=target.player;const auto *monster=target.monster;
            const int block=player && player->totals.equipment.shield?player->totals.equipment.blockChance/(player->moving && player->runningNow?3:1):monster?monster->petStats.block:0;
            if(block>0 && int(limitedRandom(random,100))<block) {
                const auto result = player
                    ? ports_.effects.block({player->player,player->actor,player->area,area->generation,0,tick.tick})
                    : ports_.monsters.block(monster->id,tick.tick);
                if(result.status==DomainStatus::Capacity) return result.status;
                commitRandom();++impact.next;continue;
            }
            const auto avoided=rollWeaponAvoidance(player?player->totals.character.combat:monster->petStats.attributes.combat,player?player->moving:monster->moving,true,random);
            if(avoided!=WeaponAvoidance::None) {
                if(player) {const auto result=ports_.effects.avoidance({player->player,player->actor,player->area,area->generation,0,tick.tick},avoided,impact.source);if(result.status==DomainStatus::Capacity) return result.status;}
                commitRandom();++impact.next;continue;
            }
        }
        MonsterHit hit=impact.monsterHit.value_or(MonsterHit{});
        auto cursor=state_.hitClassCursor;
        if(!impact.monsterHit) hit.channels[size_t(impact.type)]=impact.currentDamage();
        if(!impact.directLife) hit.hitClass=monsterDamageHitClass(hit,impact.hitClass,cursor);
        DomainResult<> result;
        if(target.player) {
            const auto &player=*target.player;
            const ActorContext actor{player.player,player.actor,player.area,area->generation,0,tick.tick};
            if(impact.directLife) result=ports_.transactions.resources(actor,player.characterRevision,
                Fixed8::fromRaw(std::max<int64_t>(256,int64_t(player.persistent.player.hp.raw())-hit.channels[size_t(impact.type)])),player.persistent.player.mana,player.persistent.player.stamina);
            else {
                const auto received=ports_.effects.receiveMonster(actor,impact.source,hit,impact.monsterStates,{},std::ref(random));
                result={received.status,received?std::optional{std::monostate{}}:std::nullopt};
            }
        } else {
            const auto &unit=*target.monster;int64_t amount=0,absorbed=0;
            if(!impact.directLife) {
                if(!retained) {
                    auto defense=ports_.effects.prepareUnitReception(unit.id,impact.source,hit,tick.tick);
                    if(!defense) return defense.status;
                    impact.environmentDefense=std::make_shared<effects::UnitReceptionPlan>(std::move(*defense.value));
                    impact.environmentDefenseCommitted=false;commitRandom();
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
        if(result.status==DomainStatus::Capacity) return result.status;
        if(result && impact.nextDelay) {if(target.player) ports_.effects.missileHitDelay(target.id(),tick.tick+impact.nextDelay);else ports_.monsters.hitDelay(target.id(),tick.tick+impact.nextDelay);}
        state_.hitClassCursor=cursor;commitRandom();impact.environmentDefense.reset();impact.environmentDefenseCommitted=false;++impact.next;
    }
    return DomainStatus::Applied;
}
}
