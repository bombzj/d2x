#include "../item_triggers.hpp"
#include "../item_trigger_runtime.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/effects/attribute_projection.hpp"
#include "server/systems/transactions/system.hpp"
#include "gameplay/rules/arithmetic.hpp"
#include <algorithm>

namespace d2x::server::reactions {
DomainResult<> ItemTriggers::advancePd2ItemHeal(ItemTrigger &job,uint64_t tick) {
    const auto done=[] {return DomainResult<>{DomainStatus::Applied,std::monostate{}};};
    if(job.phase!=ItemTriggerPhase::Notify) {
        if(!job.target) return done();
        const auto values=ports_.effects.unitStatValues(job.actor.actor,tick);if(!values) return done();
        const auto amount=values->get(*job.healStat);if(!amount) return done();
        // EventFunc28 reads the encoded stat key; this event has no chance
        // roll and no skill program. The fixed-eight-bit HP addition wraps
        // before the native zero/max clamp, just as HealAfterKill does.
        if(const auto *player=ports_.players.findActor(job.actor.actor)) {
            const auto previous=player->persistent.player.hp.raw(),maximum=player->totals.character.maxLifeFixed.raw();
            if(previous>=maximum) return done();
            const auto next=std::clamp(nativeAdd(previous,nativeShiftLeft(amount,8)),0,maximum);
            auto actor=job.actor;actor.tick=tick;
            const auto committed=ports_.transactions.resources(actor,player->characterRevision,Fixed8::fromRaw(next),
                player->persistent.player.mana,player->persistent.player.stamina);
            if(!committed) return committed;
        } else if(const auto *monster=ports_.monsters.find(job.actor.actor)) {
            if(monster->life>=monster->maximumLife) return done();
            auto resources=effects::MonsterResourceTrace(*monster,tick);
            resources.seedLife(Fixed8::fromRaw(std::clamp(nativeAdd(nativeChecked(monster->life),nativeShiftLeft(amount,8)),0,nativeChecked(monster->maximumLife))));
            if(!ports_.monsters.commitLife(resources.take())) return {DomainStatus::Stale,{}};
        } else return done();
        job.phase=ItemTriggerPhase::Notify;
    }
    const auto published=ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},job.actor.area},
        {OverlayFact{job.actor.actor,uint8_t(ports_.players.findActor(job.actor.actor)?0:1),job.actor.area,151}}});
    return {published.status,published?std::optional{std::monostate{}}:std::nullopt};
}
}
