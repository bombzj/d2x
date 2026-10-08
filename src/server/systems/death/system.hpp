#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/player/corpse.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::death {
// Death occurrences, resurrection and corpse recovery; no duplicated persistent corpse items.
enum class Action { Resurrect, RecoverCorpse };
struct Request { Action action; std::optional<UnitTarget> corpse; };
struct Transition { uint64_t occurrence{}; EntityId victim, killer; bool finalized{}; uint64_t ready{}; };
struct Reward { PlayerId player; EntityId actor; uint64_t amount{}; };
struct State { std::map<EntityId, Transition> transitions; std::map<EntityId, Reward> rewards; };
struct Ports { const PlayerStore &players; monsters::System &monsters; progression::System &progression; skills::System &skills; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
