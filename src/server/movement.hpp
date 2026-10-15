#pragma once
#include "server/area_store.hpp"
#include "server/player_state.hpp"
#include "server/runtime/contracts.hpp"
#include <map>

namespace d2x::server {
class PlayerStore;
namespace travel { class System; }
namespace monsters { class System; }
namespace effects { class System; }
namespace items { class System; }
struct ActorContext;
struct TickContext;
struct MovementPorts { PlayerStore &players; AreaStore &areas; travel::System &travel; const monsters::System &monsters; const effects::System &effects; const items::System &items; };
class MovementSystem {
    const MovementPorts ports_;
    std::map<PlayerId,uint64_t> skillSteps_;
  public:
    explicit MovementSystem(MovementPorts ports) : ports_(ports) {}
    CommandStatus execute(const ActorContext &, const MovementCommand &);
    DomainResult<bool> chargeStep(const ActorContext &,Vec target,float speed,int stopDistance);
    DomainResult<bool> leapStep(const ActorContext &,Vec target,float speed,std::optional<MovementCollisionRule> collision = {});
    void step(TickContext);
    void suspend();
};
CommandStatus applyMovement(PlayerState &, const AreaState &, const MovementCommand &);
void advanceMovement(PlayerState &, const AreaState &, float seconds);
} // namespace d2x::server
