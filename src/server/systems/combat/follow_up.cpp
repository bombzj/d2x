#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::finishImpact(SpellImpact &impact,const PlayerState *owner,uint64_t generation,uint64_t tick) {
    if(owner) {
        const ActorContext actor{owner->player,owner->actor,owner->area,generation,0,tick};
        if(owner->persistent.player.hp>0 && (impact.lifeGain || impact.manaGain || impact.contactOverlay>=0 || impact.leechOverlay>=0)) {
            std::vector<DomainFact> visuals;
            if(impact.contactOverlay>=0) visuals.emplace_back(OverlayFact{impact.targets[impact.next],1,impact.area,impact.contactOverlay});
            if(impact.leechOverlay>=0) visuals.emplace_back(OverlayFact{impact.source,0,impact.area,impact.leechOverlay});
            const auto &record=owner->persistent.player;
            const auto &attributes=owner->totals.character;
            const auto result=ports_.transactions.resources(actor,owner->characterRevision,
                std::min(float(attributes.maxLife),record.hp+float(impact.lifeGain)/256.f),
                std::min(float(attributes.maxMana),record.mana+float(impact.manaGain)/256.f),record.stamina,std::move(visuals));
            if(result.status==DomainStatus::Capacity) return result.status;
        }
        // This phase precedes Sacrifice recoil. Clear only after resource and
        // overlay admission, so retrying recoil cannot credit leech twice.
        impact.lifeGain=impact.manaGain=0;impact.contactOverlay=impact.leechOverlay=-1;
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
