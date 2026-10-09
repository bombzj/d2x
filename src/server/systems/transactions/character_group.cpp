#include "system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
#include <algorithm>
namespace d2x::server::transactions {
DomainResult<> System::commitCharacters(std::vector<Plan> plans) {
    if(plans.empty() || plans.size()>2) return {DomainStatus::InvalidRequest,{}};
    std::set<PlayerId> actors; std::vector<EventBatch> batches;
    uint64_t last=state_.lastCommitted;
    for(const auto &plan:plans) {
        const auto *e=std::get_if<CharacterEdit>(&plan.change);
        if(!e || !plan.player || (plan.player->inventoryChanged && !e->charge) || e->revival || e->knockback || e->waypoints || e->experienceAward || !actors.insert(e->actor.player).second)
            return {DomainStatus::InvalidRequest,{}};
        const auto *p=ports_.players.find(e->actor.player);
        if(!p || !p->entered || p->persistent.player.hp<=0 || p->actor!=e->actor.actor || p->area!=e->actor.area) return {DomainStatus::InvalidActor,{}};
        if(p->inventoryRevision!=e->expectedInventoryRevision || p->characterRevision!=e->expectedCharacterRevision ||
            plan.expected.size()!=2 || plan.expected[0].entity!=p->actor || plan.expected[0].expected!=p->inventoryRevision ||
            plan.expected[1].entity!=p->actor || plan.expected[1].expected!=p->characterRevision ||
            p->inventoryRevision==UINT64_MAX || p->characterRevision==UINT64_MAX ||
            !ports_.areas.find(e->actor.area) || ports_.areas.at(e->actor.area).generation!=e->actor.areaGeneration ||
            plan.id.value<=last || plan.id.value>=state_.next) return {DomainStatus::Stale,{}};
        last=plan.id.value;
        if(!e->publicFacts.empty()) batches.push_back({0,e->actor.tick,plan.id,{AudienceKind::Area,{},p->area},e->publicFacts});
        EventBatch batch{0,e->actor.tick,plan.id,{AudienceKind::Player,p->player,p->area},
            {CharacterFact{p->persistent.player,plan.player->persistent.player,p->totals,plan.player->totals,e->selectedHand}}};
        if(plan.player->inventoryChanged) {
            auto projection=plan.player->persistent;
            std::set<EntityId> retained;
            for(const auto &change:plan.player->changes) if(change.kind!=ItemChangeKind::Removed) retained.insert(change.item);
            std::erase_if(projection.inventory.items,[&](const auto &entry){return !retained.contains(entry.first);});
            projection.inventory.containers.insert(p->persistent.inventory.containers.begin(),p->persistent.inventory.containers.end());
            for(const auto &[id,item]:projection.inventory.items) for(const auto &child:item.socketedItems) projection.inventory.items.emplace(child.id,child);
            batch.facts.insert(batch.facts.begin(),InventoryFact{std::move(projection),plan.player->changes,false});
        }
        batch.facts.insert(batch.facts.end(),e->facts.begin(),e->facts.end());batches.push_back(std::move(batch));
    }
    const auto output=ports_.events.publishGroup(std::move(batches));if(!output) return {output.status,{}};
    for(auto &plan:plans) {
        auto &e=std::get<CharacterEdit>(plan.change);auto &p=ports_.players.players_.at(e.actor.player);
        if(e.transient) std::swap(p.transient,*e.transient);
        if(e.equipment) p.rules.equipment=e.equipment;
        if(plan.player->inventoryChanged) ++p.inventoryRevision;
        std::swap(p.persistent,plan.player->persistent);std::swap(p.totals,plan.player->totals);++p.characterRevision;
    }
    state_.lastCommitted=last;return {DomainStatus::Applied,std::monostate{}};
}
}
