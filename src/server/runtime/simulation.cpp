#include "simulation.hpp"
#include "game_systems.hpp"
#include <chrono>

namespace d2x::server {
void Simulation::step(TickContext tick, GameSystems &systems) {
    // Scratch facts only: reliable outputs live in EventOutbox and survive steps.
    // Each consumer must run after its producers; deferred work belongs to the
    // owning system's state, never to this per-step scratch buffer.
    facts_ = {};
    for(auto &metric:metrics_) metric.nanoseconds=0;
    const auto run=[&](SystemId id,auto &&operation) {
        const auto started=std::chrono::steady_clock::now();
        const auto status=operation();
        auto &metric=metrics_[size_t(id)];
        metric.nanoseconds+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
        ++metric.calls;if(status==StepStatus::Blocked) ++metric.blocked;
        steps_[size_t(id)]=status;
    };
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
    run(SystemId::Missiles,[&]{return systems.missiles.step(tick,facts_);});
    run(SystemId::Effects,[&]{return systems.effects.step(tick,facts_);});
    run(SystemId::Combat,[&]{return systems.combat.step(tick,facts_);});
    steps_[size_t(SystemId::Trade)] = systems.trade.step(tick);
    if (steps_[size_t(SystemId::Trade)]==StepStatus::Blocked) return;
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
}
}
