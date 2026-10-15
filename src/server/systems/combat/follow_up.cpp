#include "system.hpp"
#include "server/player_store.hpp"
#include "server/systems/monsters/system.hpp"
#include "server/systems/effects/system.hpp"
#include "server/systems/transactions/system.hpp"
#include <algorithm>

namespace d2x::server::combat {
DomainStatus System::finishImpact(SpellImpact &impact,const PlayerState *owner,uint64_t generation,uint64_t tick) {
    if(owner && impact.source!=owner->actor) {
        const auto *pet=ports_.monsters.find(impact.source);
        if(pet && pet->life>0 && (impact.lifeGain || impact.contactOverlay>=0 || impact.leechOverlay>=0)) {
            std::vector<DomainFact> visuals;
            if(impact.contactOverlay>=0) visuals.emplace_back(OverlayFact{impact.targets[impact.next],uint8_t(ports_.players.findActor(impact.targets[impact.next])?0:1),impact.area,impact.contactOverlay});
            if(impact.leechOverlay>=0 && impact.lifeGain) visuals.emplace_back(OverlayFact{impact.source,1,impact.area,impact.leechOverlay});
            if(!ports_.events.hasCapacity(visuals.size()+2,2)) return DomainStatus::Capacity;
            const auto result=ports_.monsters.heal(pet->id,impact.lifeGain);
            if(result.status==DomainStatus::Capacity) return result.status;
            if(result && !visuals.empty()) ports_.events.publish({0,tick,{}, {AudienceKind::Area,{},impact.area},std::move(visuals)});
        }
        impact.lifeGain=impact.manaGain=0;impact.contactOverlay=impact.leechOverlay=-1;
    }
    if(owner) {
        const ActorContext actor{owner->player,owner->actor,owner->area,generation,0,tick};
        if(owner->persistent.player.hp>0 && (impact.lifeGain || impact.manaGain || impact.contactOverlay>=0 || impact.leechOverlay>=0)) {
            std::vector<DomainFact> visuals;
            if(impact.contactOverlay>=0) visuals.emplace_back(OverlayFact{impact.targets[impact.next],uint8_t(ports_.players.findActor(impact.targets[impact.next])?0:1),impact.area,impact.contactOverlay});
            if(impact.leechOverlay>=0) visuals.emplace_back(OverlayFact{impact.source,0,impact.area,impact.leechOverlay});
            const auto &record=owner->persistent.player;
            const auto &attributes=owner->totals.character;
            const auto result=ports_.transactions.resources(actor,owner->characterRevision,
                Fixed8::fromRaw(std::min<int64_t>(attributes.maxLifeFixed.raw(),int64_t(record.hp.raw())+impact.lifeGain)),
                Fixed8::fromRaw(std::min<int64_t>(attributes.maxManaFixed.raw(),int64_t(record.mana.raw())+impact.manaGain)),record.stamina,std::move(visuals));
            if(result.status==DomainStatus::Capacity) return result.status;
        }
        // This phase precedes Sacrifice recoil. Clear only after resource and
        // overlay admission, so retrying recoil cannot credit leech twice.
        impact.lifeGain=impact.manaGain=0;impact.contactOverlay=impact.leechOverlay=-1;
        if(impact.conversion && impact.phase==ImpactPhase::FollowUp) {
            const auto result=ports_.effects.convert(actor,impact.targets[impact.next],*impact.conversion);
            if(result.status==DomainStatus::Capacity) return result.status;
        }
        impact.phase=ImpactPhase::Recoil;
        if(owner->persistent.player.hp>0 && impact.selfDamage>0) {
            const int64_t life=int64_t(owner->persistent.player.hp.raw());
            const auto result=ports_.transactions.damage(actor,owner->characterRevision,life-impact.selfDamage<256?life:impact.selfDamage);
            if(result.status==DomainStatus::Capacity) return result.status;
        }
    }
    impact.phase=ImpactPhase::Contact;impact.selfDamage=0;++impact.next;
    return DomainStatus::Applied;
}
}
