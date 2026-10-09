#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/player/corpse.hpp"
#include "server/systems/companions/preparation.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::death {
// Death occurrences, resurrection and corpse recovery; no duplicated persistent corpse items.
enum class Action { Resurrect, RecoverCorpse };
struct Request { Action action; std::optional<UnitTarget> corpse; };
struct Transition { uint64_t occurrence{}; EntityId victim, killer; bool finalized{}; uint64_t ready{}; bool reviving{}; bool companionsSettled{}; };
struct Reward { PlayerId player; EntityId actor; uint64_t amount{}; bool lootQueued{}; int life{},mana{};bool restored{}; CharacterRecord lootClaimant; std::optional<companions::HirelingExperienceAward> hireling{}; bool hirelingAwarded{}; };
struct Recovery { ActorContext actor; EntityId corpse; uint64_t locomotion{}; };
struct State { std::map<EntityId, Transition> transitions; std::map<EntityId, Reward> rewards; std::map<PlayerId, Recovery> recoveries; };
struct Ports { const PlayerStore &players; monsters::System &monsters; progression::System &progression; skills::System &skills; loot::System &loot; items::System &items; transactions::System &transactions; world::System &world; MovementSystem &movement; const AreaStore &areas; const GameSettings &settings; companions::System &companions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
    DomainResult<> settle(const ActorContext &);
    DomainResult<> recover(const ActorContext &, EntityId);
    void advanceRecovery(TickContext);
};
}
