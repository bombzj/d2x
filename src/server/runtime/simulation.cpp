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
    if(systems.effects.finishNativeStateRemovals(tick.tick,{})==StepStatus::Blocked) return false;
    if(systems.monsters.finishModifiers(tick)!=StepStatus::Complete) return false;
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
                event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::AiThink?SystemId::Ai:
                event.owner.type==FrameEventUnit::Monster && event.type==FrameEventType::ModeChange && !event.payload?SystemId::Monsters:
                (event.owner.type==FrameEventUnit::Player || event.owner.type==FrameEventUnit::Monster) && (event.type==FrameEventType::ModeChange || event.type==FrameEventType::EndAnimation)?SystemId::Skills:
                event.owner.type==FrameEventUnit::Monster && (event.type==FrameEventType::AiReset || event.type==FrameEventType::StatRegeneration)?SystemId::Monsters:SystemId::Effects;
            DomainResult<> result{DomainStatus::Applied,std::monostate{}};
            if(completedNativeEvent_!=event.handle.value) run(owner,[&] {
                result=dispatchFrameEvent(systems,event,tick);
                return isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale?
                    StepStatus::Blocked:StepStatus::Complete;
            });
            if(isDeferred(result.status) || result.status==DomainStatus::Conflict || result.status==DomainStatus::Stale) return false;
            if(!result && result.status!=DomainStatus::InvalidActor) throw std::logic_error("Native frame event has no implemented handler");
            completedNativeEvent_=event.handle.value;
            if(systems.effects.finishNativeStateRemovals(tick.tick,{})==StepStatus::Blocked) return false;
            completedNativeEvent_=0;
            return true;
        })) return false;
        phase_=Phase::Domains;
    }
    if(phase_==Phase::Domains) {
    run(SystemId::Social,[&]{systems.social.step(tick);return StepStatus::Complete;});
    run(SystemId::World,[&]{return systems.world.step(tick,facts_);});
    phase_=Phase::Population;
    }
    if(phase_==Phase::Population) {
    run(SystemId::Population,[&]{return systems.population.step(tick,facts_);});
    if(steps_[size_t(SystemId::Population)]!=StepStatus::Complete) return false;
    phase_=Phase::Actors;
    }
    if(phase_==Phase::Actors) {
    run(SystemId::Attributes,[&]{return systems.attributes.step(tick,facts_);});
    if(steps_[size_t(SystemId::Attributes)]==StepStatus::Blocked) return false;
    run(SystemId::Spatial,[&]{return systems.spatial.step(tick,facts_);});
    phase_=Phase::Companions;
    }
    if(phase_==Phase::Companions) {
    run(SystemId::Companions,[&]{return systems.companions.step(tick,facts_);});
    if(steps_[size_t(SystemId::Companions)]==StepStatus::Blocked) return false;
    run(SystemId::Ai,[&]{return systems.ai.step(tick,facts_);});
    phase_=Phase::Skills;
    }
    if(phase_==Phase::Skills) {
    run(SystemId::Skills,[&]{return systems.skills.step(tick,facts_);});
    if(steps_[size_t(SystemId::Skills)]==StepStatus::Blocked) return false;
    phase_=Phase::Movement;
    }
    if(phase_==Phase::Movement) {
    run(SystemId::Movement,[&]{systems.movement.step(tick);return StepStatus::Complete;});
    run(SystemId::Inventory,[&]{return systems.inventory.step(tick,facts_);});
    run(SystemId::Items,[&]{return systems.items.step(tick,facts_);});
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
    phase_=Phase::StateRemovals;
    }
    if(phase_==Phase::StateRemovals) {
    if(systems.effects.finishNativeStateRemovals(tick.tick,{})==StepStatus::Blocked) return false;
    phase_=Phase::Trade;
    }
    if(phase_==Phase::Trade) {
    run(SystemId::Trade,[&]{return systems.trade.step(tick);});
    if (steps_[size_t(SystemId::Trade)]==StepStatus::Blocked) return false;
    phase_=Phase::Tail;
    }
    if(phase_==Phase::Tail) {
    run(SystemId::Death,[&]{return systems.death.step(tick,facts_);});
    run(SystemId::Npc,[&]{return systems.npc.step(tick,facts_);});
    run(SystemId::Merchant,[&]{return systems.merchant.step(tick,facts_);});
    run(SystemId::Crafting,[&]{return systems.crafting.step(tick,facts_);});
    phase_=Phase::Objects;
    }
    if(phase_==Phase::Objects) {
    run(SystemId::Objects,[&]{return systems.objects.step(tick,facts_);});
    if(steps_[size_t(SystemId::Objects)]!=StepStatus::Complete) return false;
    if(systems.monsters.pendingModifiers()) return false;
    phase_=Phase::ObjectRemovals;
    }
    if(phase_==Phase::ObjectRemovals) {
    if(systems.effects.finishNativeStateRemovals(tick.tick,{})==StepStatus::Blocked) return false;
    phase_=Phase::Modifiers;
    }
    if(phase_==Phase::Modifiers) {
        if(systems.monsters.finishModifiers(tick)!=StepStatus::Complete) return false;
        phase_=Phase::Quests;
    }
    if(phase_==Phase::Quests) {
    run(SystemId::Quests,[&]{return systems.quests.step(tick,facts_);});
    phase_=Phase::Tower;
    }
    if(phase_==Phase::Tower) {
        const auto status=systems.objects.towerStep(tick);
        if(status!=StepStatus::Complete) {steps_[size_t(SystemId::Objects)]=status;return false;}
        phase_=Phase::Publication;
    }
    run(SystemId::Loot,[&]{return systems.loot.step(tick,facts_);});
    run(SystemId::Progression,[&]{return systems.progression.step(tick,facts_);});
    run(SystemId::Travel,[&]{return systems.travel.step(tick,facts_);});
    run(SystemId::Replication,[&]{return systems.replication.step(tick,facts_);});
    phase_=Phase::Environment;frame_.reset();return true;
}
}
