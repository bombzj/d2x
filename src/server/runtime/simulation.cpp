#include "simulation.hpp"
#include "game_systems.hpp"

namespace d2x::server {
void Simulation::step(TickContext tick, GameSystems &systems) {
    // Scratch facts only: reliable outputs live in EventOutbox and survive steps.
    // Each consumer must run after its producers; deferred work belongs to the
    // owning system's state, never to this per-step scratch buffer.
    facts_ = {};
    steps_[size_t(SystemId::World)] = systems.world.step(tick, facts_);
    steps_[size_t(SystemId::Population)] = systems.population.step(tick, facts_);
    steps_[size_t(SystemId::Attributes)] = systems.attributes.step(tick, facts_);
    steps_[size_t(SystemId::Spatial)] = systems.spatial.step(tick, facts_);
    steps_[size_t(SystemId::Companions)] = systems.companions.step(tick, facts_);
    steps_[size_t(SystemId::Ai)] = systems.ai.step(tick, facts_);
    steps_[size_t(SystemId::Skills)] = systems.skills.step(tick, facts_);
    systems.movement.step(tick);
    steps_[size_t(SystemId::Movement)] = StepStatus::Complete;
    steps_[size_t(SystemId::Inventory)] = systems.inventory.step(tick, facts_);
    steps_[size_t(SystemId::Items)] = systems.items.step(tick, facts_);
    steps_[size_t(SystemId::Monsters)] = systems.monsters.step(tick, facts_);
    steps_[size_t(SystemId::Spatial)] = systems.spatial.step(tick, facts_);
    steps_[size_t(SystemId::Missiles)] = systems.missiles.step(tick, facts_);
    steps_[size_t(SystemId::Effects)] = systems.effects.step(tick, facts_);
    steps_[size_t(SystemId::Combat)] = systems.combat.step(tick, facts_);
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
