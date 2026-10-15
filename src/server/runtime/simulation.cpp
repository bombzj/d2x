#include "simulation.hpp"
#include "game_systems.hpp"
#include "frame_event_dispatch.hpp"
#include <chrono>
#include <stdexcept>

namespace d2x::server {
bool Simulation::step(TickContext tick, GameSystems &systems) {
    // Scratch facts only: reliable outputs live in EventOutbox and survive steps.
    // Each consumer must run after its producers; deferred work belongs to the
    // owning system's state, never to this per-step scratch buffer.
    if(!frame_) {frame_=tick.tick;facts_={};for(auto &metric:metrics_) metric.nanoseconds=0;}
    else if(*frame_!=tick.tick) throw std::logic_error("Simulation resumed an unfinished frame with another timestamp");
    const auto run=[&](SystemId id,auto &&operation) {
        const auto started=std::chrono::steady_clock::now();
        const auto status=operation();
        auto &metric=metrics_[size_t(id)];
        metric.nanoseconds+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
        ++metric.calls;if(status==StepStatus::Blocked) ++metric.blocked;
        steps_[size_t(id)]=status;
    };
    if(phase_==Phase::Environment) {
        for(;;) {
            const auto status=systems.world.advanceEnvironment(tick);
            if(status==StepStatus::Complete) break;
            run(SystemId::Attributes,[&]{return systems.attributes.refreshEnvironment(tick);});
            if(steps_[size_t(SystemId::Attributes)]==StepStatus::Blocked) return false;
        }
        phase_=Phase::Events;
    }
    if(phase_==Phase::Events) {
        run(SystemId::Spatial,[&]{return systems.spatial.step(tick,facts_);});
        if(steps_[size_t(SystemId::Spatial)]==StepStatus::Blocked) return false;
        if(!systems.frameEvents.advance(tick.tick,[&](const FrameEvent &event) {
            const auto owner=event.owner.type==FrameEventUnit::Missile?SystemId::Missiles:
                event.owner.type==FrameEventUnit::Monster && (event.type==FrameEventType::AiReset || event.type==FrameEventType::StatRegeneration)?SystemId::Monsters:SystemId::Effects;
            DomainResult<> result;
            run(owner,[&] {
                result=dispatchFrameEvent(systems,event,tick);
                return result.status==DomainStatus::Capacity || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale?
                    StepStatus::Blocked:StepStatus::Complete;
            });
            if(result.status==DomainStatus::Capacity || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return false;
            if(!result && result.status!=DomainStatus::InvalidActor) throw std::logic_error("Native frame event has no implemented handler");
            return true;
        })) return false;
        phase_=Phase::Domains;
    }
    if(phase_==Phase::Domains) {
    systems.social.step(tick);
    steps_[size_t(SystemId::Social)] = StepStatus::Complete;
    steps_[size_t(SystemId::World)] = systems.world.step(tick, facts_);
    steps_[size_t(SystemId::Population)] = systems.population.step(tick, facts_);
    steps_[size_t(SystemId::Attributes)] = systems.attributes.step(tick, facts_);
    run(SystemId::Spatial,[&]{return systems.spatial.step(tick,facts_);});
    run(SystemId::Companions,[&]{return systems.companions.step(tick,facts_);});
    run(SystemId::Ai,[&]{return systems.ai.step(tick,facts_);});
    run(SystemId::Skills,[&]{return systems.skills.step(tick,facts_);});
    systems.movement.step(tick);
    steps_[size_t(SystemId::Movement)] = StepStatus::Complete;
    steps_[size_t(SystemId::Inventory)] = systems.inventory.step(tick, facts_);
    steps_[size_t(SystemId::Items)] = systems.items.step(tick, facts_);
    run(SystemId::Monsters,[&]{return systems.monsters.step(tick,facts_);});
    run(SystemId::Spatial,[&]{return systems.spatial.step(tick,facts_);});
    phase_=Phase::Effects;
    }
    if(phase_==Phase::Effects) {
    run(SystemId::Effects,[&]{return systems.effects.step(tick,facts_);});
    if(steps_[size_t(SystemId::Effects)]==StepStatus::Blocked) return false;
    phase_=Phase::Combat;
    }
    if(phase_==Phase::Combat) {
    run(SystemId::Combat,[&]{return systems.combat.step(tick,facts_);});
    if(steps_[size_t(SystemId::Combat)]==StepStatus::Blocked) return false;
    phase_=Phase::Trade;
    }
    if(phase_==Phase::Trade) {
    steps_[size_t(SystemId::Trade)] = systems.trade.step(tick);
    if (steps_[size_t(SystemId::Trade)]==StepStatus::Blocked) return false;
    phase_=Phase::Tail;
    }
    steps_[size_t(SystemId::Death)] = systems.death.step(tick, facts_);
    steps_[size_t(SystemId::Npc)] = systems.npc.step(tick, facts_);
    steps_[size_t(SystemId::Merchant)] = systems.merchant.step(tick, facts_);
    steps_[size_t(SystemId::Crafting)] = systems.crafting.step(tick, facts_);
    steps_[size_t(SystemId::Objects)] = systems.objects.step(tick, facts_);
    steps_[size_t(SystemId::Quests)] = systems.quests.step(tick, facts_);
    steps_[size_t(SystemId::Loot)] = systems.loot.step(tick, facts_);
    steps_[size_t(SystemId::Progression)] = systems.progression.step(tick, facts_);
    steps_[size_t(SystemId::Travel)] = systems.travel.step(tick, facts_);
    steps_[size_t(SystemId::Replication)] = systems.replication.step(tick, facts_);
    phase_=Phase::Environment;frame_.reset();return true;
}
}
