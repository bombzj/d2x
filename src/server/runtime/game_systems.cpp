#include "game_systems.hpp"

namespace d2x::server {
// Constructors only retain Ports; they must not inspect/call peer systems.
// Every peer is ready before admission/commands/steps begin. Reference lifetime
// is guaranteed by the non-movable instance; destruction performs no callbacks.
GameSystems::GameSystems(PlayerStore &players, AreaStore &areas, EntityIds &ids, uint64_t &random,
                         EventOutbox &events, const PreparedRules &rules, const GameSettings &settings)
    : movement(MovementPorts{players, areas}),
      world(d2x::server::world::Ports{areas, players, settings, ids}),
      spatial(d2x::server::spatial::Ports{areas, players, monsters, objects, missiles, items}),
      items(d2x::server::items::Ports{players, ids, random, rules.items.get()}),
      inventory(d2x::server::inventory::Ports{players, items, transactions, rules.items.get()}),
      attributes(d2x::server::attributes::Ports{players}),
      crafting(d2x::server::crafting::Ports{players, items, transactions, rules.items.get()}),
      loot(d2x::server::loot::Ports{players, quests, items, transactions, random, rules.treasure.get(), settings}),
      population(d2x::server::population::Ports{areas, monsters, random, settings}),
      monsters(d2x::server::monsters::Ports{areas, players, ids, random, events}),
      ai(d2x::server::ai::Ports{monsters, players, areas, skills, random}),
      skills(d2x::server::skills::Ports{players, areas, monsters, movement, combat, transactions, missiles, travel, events}),
      missiles(d2x::server::missiles::Ports{areas, players, monsters, combat, transactions, ids, random}),
      effects(d2x::server::effects::Ports{players, monsters, spatial, transactions, ids, rules.effects.get()}),
      combat(d2x::server::combat::Ports{players, monsters, areas, transactions, random, events}),
      death(d2x::server::death::Ports{players, monsters, progression, skills}),
      companions(d2x::server::companions::Ports{players, monsters, skills, transactions}),
      objects(d2x::server::objects::Ports{players, areas, quests, loot, effects, transactions, ids}),
      npc(d2x::server::npc::Ports{players, monsters, quests, spatial}),
      merchant(d2x::server::merchant::Ports{players, npc, items, transactions, random, rules.items.get()}),
      quests(d2x::server::quests::Ports{players, objects, transactions}),
      progression(d2x::server::progression::Ports{players, transactions}),
      travel(d2x::server::travel::Ports{players, areas, world, trade, npc, transactions, events}),
      social(d2x::server::social::Ports{players, events}),
      trade(d2x::server::trade::Ports{players, items, transactions}),
      transactions(d2x::server::transactions::Ports{players, areas, events}),
      replication(d2x::server::replication::Ports{players, areas, spatial, monsters, events}) {}
}
