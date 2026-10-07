#include "game_systems.hpp"

namespace d2x::server {
// Constructors only retain Ports; they must not inspect/call peer systems.
// Every peer is ready before admission/commands/steps begin. Reference lifetime
// is guaranteed by the non-movable instance; destruction performs no callbacks.
GameSystems::GameSystems(PlayerStore &players, AreaStore &areas, EntityIds &ids, uint64_t &random,
                         EventOutbox &events, const PreparedRules &rules, const GameSettings &settings)
    : movement(MovementPorts{players, areas}),
      world(d2x::server::world::Ports{areas, players, settings}),
      spatial(d2x::server::spatial::Ports{areas, players, monsters, objects, missiles, items}),
      items(d2x::server::items::Ports{players, ids, random, rules.items.get()}),
      inventory(d2x::server::inventory::Ports{players, items, transactions, rules.items.get()}),
      attributes(d2x::server::attributes::Ports{players, items, effects, companions}),
      crafting(d2x::server::crafting::Ports{players, items, transactions, rules.items.get()}),
      loot(d2x::server::loot::Ports{players, quests, items, transactions, random, rules.treasure.get(), settings}),
      population(d2x::server::population::Ports{areas, monsters, random, settings}),
      monsters(d2x::server::monsters::Ports{areas, spatial, attributes, ids}),
      ai(d2x::server::ai::Ports{monsters, spatial, social, skills}),
      skills(d2x::server::skills::Ports{players, attributes, spatial, combat, missiles, effects, companions, rules.skills.get()}),
      missiles(d2x::server::missiles::Ports{areas, spatial, combat, ids}),
      effects(d2x::server::effects::Ports{players, monsters, spatial, transactions, ids, rules.effects.get()}),
      combat(d2x::server::combat::Ports{players, monsters, attributes, social, transactions, random}),
      death(d2x::server::death::Ports{players, monsters, transactions, trade}),
      companions(d2x::server::companions::Ports{players, monsters, skills, transactions}),
      objects(d2x::server::objects::Ports{players, areas, quests, loot, effects, transactions, ids}),
      npc(d2x::server::npc::Ports{players, monsters, quests, spatial}),
      merchant(d2x::server::merchant::Ports{players, npc, items, transactions, random, rules.items.get()}),
      quests(d2x::server::quests::Ports{players, objects, transactions}),
      progression(d2x::server::progression::Ports{players, social, transactions}),
      travel(d2x::server::travel::Ports{players, areas, world, trade, npc, transactions}),
      social(d2x::server::social::Ports{players}),
      trade(d2x::server::trade::Ports{players, items, transactions}),
      transactions(d2x::server::transactions::Ports{players, items, events}),
      replication(d2x::server::replication::Ports{players, areas, spatial, events}) {}
}
