#include "system.hpp"
#include <stdexcept>
#include "server/systems/monsters/system.hpp"
#include "server/player_store.hpp"
#include "server/area_store.hpp"
namespace d2x::server::missiles {
StepStatus System::step(TickContext tick, FrameFacts &) {
    bool blocked=false;
    // Newly committed children start on the next domain frame.
    const auto end=ports_.ids.cursor();
    for(auto it=state_.missiles.begin();it!=state_.missiles.end() && it->first.value<end;) {
        const auto &m=it->second;
        const auto *p=ports_.players.find(m.player);const auto *a=ports_.areas.find(m.area);
        const auto *enemy=m.enemy?ports_.monsters.find(m.owner):nullptr;
        const bool validOwner=m.enemy?enemy && !enemy->owner && enemy->area==m.area:p && p->entered && p->actor==m.owner && p->area==m.area;
        if(!validOwner || !a || a->generation!=m.generation || a->definition.town) {
            pending_.erase(it->first);it=state_.missiles.erase(it);continue;
        }
        if(tick.tick<=m.created) {++it;continue;}
        auto [entry,fresh]=pending_.try_emplace(m.id);if(fresh) entry->second=advance(m);
        auto &plan=entry->second;
        if(plan.children.size()>4096-state_.missiles.size() || plan.children.size()>UINT32_MAX-ports_.ids.cursor()) {blocked=true;++it;continue;}
        auto facts=visuals(plan.children);
        if(!facts.empty() && !ports_.events.hasCapacity(facts.size())) {blocked=true;++it;continue;}
        auto impacts=ports_.combat.prepareSpells(plan.impacts);
        if(!impacts) {if(impacts.status!=DomainStatus::Capacity) throw std::logic_error("Invalid staged missile impact");blocked=true;++it;continue;}
        std::map<EntityId,Missile> children;auto cursor=ports_.ids.cursor();
        for(auto child:plan.children) {child.id=EntityId{cursor++};child.created=tick.tick;child.expires=tick.tick+uint64_t(child.lifetimeFrames);children.emplace(child.id,std::move(child));}
        if(!facts.empty() && !ports_.events.publish({0,tick.tick,{}, {AudienceKind::Area,{},m.area},std::move(facts)})) {blocked=true;++it;continue;}
        ports_.combat.commitSpells(std::move(*impacts.value));
        for(size_t i=0;i<children.size();++i) ports_.ids.allocate();
    state_.missiles.merge(children);
        const bool finished=plan.finished;it->second=std::move(plan.next);pending_.erase(entry);
        if(finished) it=state_.missiles.erase(it);else ++it;
    }
    return blocked?StepStatus::Blocked:StepStatus::Complete;
}
}
