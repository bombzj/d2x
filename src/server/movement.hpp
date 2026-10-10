#pragma once
#include "server/area_store.hpp"
#include "server/player_state.hpp"
#include "server/runtime/contracts.hpp"
#include <map>

namespace d2x::server {
class PlayerStore;
namespace travel { class System; }
struct ActorContext;
struct TickContext;
struct MovementPorts { PlayerStore &players; const AreaStore &areas; const travel::System &travel; };
class MovementSystem {
    const MovementPorts ports_;
    std::map<PlayerId,uint64_t> skillSteps_;
  public:
    explicit MovementSystem(MovementPorts ports) : ports_(ports) {}
    CommandStatus execute(const ActorContext &, const MovementCommand &);
    DomainResult<bool> chargeStep(const ActorContext &,Vec target,float speed,int stopDistance);
    void step(TickContext);
    void suspend();
};
CommandStatus applyMovement(PlayerState &, const AreaState &, const MovementCommand &);
void advanceMovement(PlayerState &, const AreaState &, float seconds);
} // namespace d2x::server
