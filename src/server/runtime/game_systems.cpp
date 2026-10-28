#include "game_systems.hpp"

namespace d2x::server {
// Constructors only retain Ports; they must not inspect/call peer systems.
// Every peer is ready before admission/commands/steps begin. Reference lifetime
// is guaranteed by the non-movable instance; destruction performs no callbacks.
GameSystems::GameSystems(PlayerStore &players, AreaStore &areas, EntityIds &ids, uint64_t &random,
                         EventOutbox &events, const PreparedRules &rules, const GameSettings &settings)
    : pd2ProgramRandom(uint32_t(random)),
      movement(MovementPorts{players, areas, travel, monsters, effects, items,objects,frameEvents,spatial}),
      actionState(frameEvents),
      itemTriggers(reactions::ItemTriggerPorts{players, areas, skills, transactions, random, events,monsters,effects,frameEvents}),
      world(d2x::server::world::Ports{areas, players, settings, ids, travel, monsters, missiles, skills, objects, loot,spatial,population,maps,items}),
      spatial(d2x::server::spatial::Ports{areas, players, monsters, objects, missiles, items,travel}),
      items(d2x::server::items::Ports{players, areas, ids, random, rules.items.get(), events,frameEvents,spatial,rules.itemSave}),
      inventory(d2x::server::inventory::Ports{players, items, transactions, rules.items.get(), movement, effects, areas, events, travel, trade, quests, social}),
      attributes(d2x::server::attributes::Ports{players,areas,transactions,world}),
      crafting(d2x::server::crafting::Ports{players, areas, items, transactions, rules.items.get(), inventory, settings, random, npc, travel, events, quests,pd2ProgramRandom,maps}),
      loot(d2x::server::loot::Ports{players, quests, items, transactions, random, rules.treasure.get(), settings, events, social,monsters,objects,areas,spatial,pd2ProgramRandom}),
      population(d2x::server::population::Ports{areas, monsters, random, settings, objects,spatial,quests,players,world,maps,travel,items}),
      monsters(d2x::server::monsters::Ports{areas, players, ids, random, events, settings, effects, social,frameEvents,rules.skills,loot,spatial,itemTriggers,travel,maps}),
      ai(d2x::server::ai::Ports{monsters, players, areas, skills, objects, social, effects, spatial,quests,travel,pd2ProgramRandom,loot,maps,world,npc,events}),
      skills(d2x::server::skills::Ports{players, areas, monsters, movement, combat, transactions, missiles, travel, events, effects, companions, objects, inventory, loot, social, actionState, itemTriggers, spatial,frameEvents,ai,pd2ProgramRandom,maps}),
      missiles(d2x::server::missiles::Ports{areas, players, monsters, combat, transactions, ids, random, events,effects,spatial,social,frameEvents,actionState,rules.skills,quests,world,objects,companions,pd2ProgramRandom}),
      effects(d2x::server::effects::Ports{players, areas, skills, transactions, missiles, combat, monsters, events, random, social, itemTriggers, actionState,frameEvents,spatial}),
      combat(d2x::server::combat::Ports{players, monsters, areas, transactions, events, effects,inventory,social,itemTriggers,missiles,actionState,pd2ProgramRandom}),
      death(d2x::server::death::Ports{players, monsters, progression, skills, loot, items, transactions, world, movement, areas, settings, companions, social, effects,maps}),
      maps(d2x::server::maps::Ports{players,areas,monsters,travel,world,loot,transactions,pd2ProgramRandom,spatial,objects,events,frameEvents,population,effects}),
      companions(d2x::server::companions::Ports{players, monsters, skills, transactions, missiles, areas, events, random, effects, npc, inventory, combat, social, spatial,ai,items}),
      objects(d2x::server::objects::Ports{players, areas, quests, loot, effects, transactions, ids, world, monsters, settings, inventory, travel, skills, events, combat, missiles, movement, random, spatial, items,frameEvents,population,maps}),
      npc(d2x::server::npc::Ports{players, areas, transactions, events, settings, quests, effects,world, movement,monsters}),
      merchant(d2x::server::merchant::Ports{players, npc, items, transactions, events, random, rules.items.get(), settings, loot,pd2ProgramRandom}),
      quests(d2x::server::quests::Ports{players, areas, population, monsters, npc, transactions, events, settings, items, travel, world, random, loot, effects, social, itemTriggers,missiles,objects,spatial}),
      progression(d2x::server::progression::Ports{players, transactions, itemTriggers}),
      travel(d2x::server::travel::Ports{players, areas, world, trade, npc, transactions, events, inventory, items, objects, quests, social, random, settings,spatial,pd2ProgramRandom}),
      social(d2x::server::social::Ports{players, events, trade, areas, travel}),
      trade(d2x::server::trade::Ports{players, items, transactions, areas, events, inventory, npc}),
      transactions(d2x::server::transactions::Ports{players, areas, events, items, actionState, effects,random,spatial}),
      replication(d2x::server::replication::Ports{players, areas, spatial, monsters, events, effects}) {}
}
