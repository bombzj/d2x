#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/prepared_rules.hpp"
#include "server/runtime/events.hpp"
#include "gameplay/skills/spec.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::skills {
// Skill selection and cast/channel lifecycle; formulas use prepared pure skill definitions.
enum class Action { Select, Cast, Stop, Bind };
struct Request { Action action; uint16_t skill{}; bool right{}, repeat{}, stationary{}; std::optional<ActionTarget> target; std::optional<unsigned> hotkey; };
// Internal AI/companion action, not a player identity supplied over the wire.
struct CastRequest { EntityId actor; uint16_t skill{}; ActionTarget target; };
struct Cast { EntityId actor; uint16_t skill{}; uint64_t started{}, revision{}; };
struct State { std::map<EntityId, Cast> casts; };
struct Ports { const PlayerStore &players; const attributes::System &attributes; const spatial::System &spatial; combat::System &combat; missiles::System &missiles; effects::System &effects; companions::System &companions; const SkillRules *definitions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<> requestCast(const CastRequest &);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
