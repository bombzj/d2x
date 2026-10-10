#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"

namespace d2x::server::combat {
DomainStatus System::finishImpact(SpellImpact &impact,const PlayerState *owner,uint64_t generation,uint64_t tick) {
    if(owner) {
        const ActorContext actor{owner->player,owner->actor,owner->area,generation,0,tick};
        if(impact.conversion && !impact.conversionApplied) {
            const auto result=ports_.effects.convert(actor,impact.targets[impact.next],*impact.conversion);
            if(result.status==DomainStatus::Capacity) return result.status;
            impact.conversionApplied=true;
        }
        if(owner->persistent.player.hp>0 && impact.selfDamage>0) {
            const int64_t life=int64_t(owner->persistent.player.hp*256.f);
            const auto result=ports_.transactions.damage(actor,owner->characterRevision,life-impact.selfDamage<256?life:impact.selfDamage);
            if(result.status==DomainStatus::Capacity) return result.status;
        }
    }
    impact.targetApplied=false;impact.conversionApplied=false;impact.selfDamage=0;++impact.next;
    return DomainStatus::Applied;
}
}