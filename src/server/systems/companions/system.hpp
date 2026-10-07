#pragma once
#include "server/runtime/contracts.hpp"
#include "server/runtime/ports.hpp"
#include "server/runtime/events.hpp"
#include "server/systems/monsters/system.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace d2x::server::companions {
// Ownership and control for hirelings/summons; live actors belong to Monsters.
enum class Kind { Hireling, Summon, IronGolem };
enum class Action { Hire, Resurrect, Dismiss };
struct Request { Action action; EntityId npc; std::optional<uint32_t> offer; };
struct Summon { PlayerId owner; Kind kind; monsters::Admission actor; std::optional<uint16_t> sourceSkill; };
struct Companion { EntityId actor; PlayerId owner; Kind kind; std::optional<uint16_t> sourceSkill; };
struct State { std::map<EntityId, Companion> companions; };
struct Ports { const PlayerStore &players; monsters::System &monsters; skills::System &skills; transactions::System &transactions; };
class System {
    State state_;
    const Ports ports_;
  public:
    explicit System(Ports ports) : ports_(ports) {}
    const State &read() const { return state_; }
    DomainResult<EntityId> summon(const Summon &);
    DomainResult<> execute(const ActorContext &, const Request &);
    StepStatus step(TickContext, FrameFacts &);
};
}
