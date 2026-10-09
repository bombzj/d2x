#include "game_systems.hpp"

namespace d2x::server {
// Constructors only retain Ports; they must not inspect/call peer systems.
// Every peer is ready before admission/commands/steps begin. Reference lifetime
// is guaranteed by the non-movable instance; destruction performs no callbacks.
GameSystems::GameSystems(PlayerStore &players, AreaStore &areas, EntityIds &ids, uint64_t &random,
                         EventOutbox &events, const PreparedRules &rules, const GameSettings &settings)
    : movement(MovementPorts{players, areas, travel}),
      world(d2x::server::world::Ports{areas, players, settings, ids}),
      spatial(d2x::server::spatial::Ports{areas, players, monsters, objects, missiles, items}),
      items(d2x::server::items::Ports{players, areas, ids, random, rules.items.get(), events}),
      inventory(d2x::server::inventory::Ports{players, items, transactions, rules.items.get(), movement, effects, areas, events, travel, trade}),
      attributes(d2x::server::attributes::Ports{players}),
      crafting(d2x::server::crafting::Ports{players, areas, items, transactions, rules.items.get(), inventory, settings, random, npc, travel, events}),
      loot(d2x::server::loot::Ports{players, quests, items, transactions, random, rules.treasure.get(), settings, events}),
      population(d2x::server::population::Ports{areas, monsters, random, settings}),
      monsters(d2x::server::monsters::Ports{areas, players, ids, random, events}),
      ai(d2x::server::ai::Ports{monsters, players, areas, skills, objects, random}),
      skills(d2x::server::skills::Ports{players, areas, monsters, movement, combat, transactions, missiles, travel, events, effects, companions, objects, inventory}),
      missiles(d2x::server::missiles::Ports{areas, players, monsters, combat, transactions, ids, random, events,effects}),
      effects(d2x::server::effects::Ports{players, areas, skills, transactions, missiles, combat, monsters, events, random}),
      combat(d2x::server::combat::Ports{players, monsters, areas, transactions, random, events, effects,inventory}),
      death(d2x::server::death::Ports{players, monsters, progression, skills, loot, items, transactions, world, movement, areas, settings, companions}),
      companions(d2x::server::companions::Ports{players, monsters, skills, transactions, missiles, areas, events, random, effects, npc}),
      objects(d2x::server::objects::Ports{players, areas, quests, loot, effects, transactions, ids, world, monsters, settings, inventory, travel, skills, events}),
      npc(d2x::server::npc::Ports{players, areas, transactions, events, settings, quests, effects,world}),
      merchant(d2x::server::merchant::Ports{players, npc, items, transactions, events, random, rules.items.get(), settings, loot}),
      quests(d2x::server::quests::Ports{players, areas, population, monsters, npc, transactions, events, settings, items, travel, world, random, loot, effects}),
      progression(d2x::server::progression::Ports{players, transactions, effects}),
      travel(d2x::server::travel::Ports{players, areas, world, trade, npc, transactions, events, inventory, items, objects, quests}),
      social(d2x::server::social::Ports{players, events, trade}),
      trade(d2x::server::trade::Ports{players, items, transactions, areas, events, inventory, npc}),
      transactions(d2x::server::transactions::Ports{players, areas, events, items}),
      replication(d2x::server::replication::Ports{players, areas, spatial, monsters, events, effects}) {}
}
