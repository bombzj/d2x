#pragma once
#include "server/area_store.hpp"
#include "server/player_state.hpp"
#include "server/runtime/contracts.hpp"
#include "gameplay/events/frame_queue.hpp"
#include <map>
#include <set>

namespace d2x::server {
class PlayerStore;
namespace travel { class System; }
namespace monsters { class System; }
namespace effects { class System; }
namespace items { class System; }
struct ActorContext;
struct TickContext;
inline constexpr uint32_t movementTimerHandler=0x4d4f5645;
struct MovementPorts { PlayerStore &players; AreaStore &areas; travel::System &travel; const monsters::System &monsters; const effects::System &effects; const items::System &items; FrameEventQueue &frameEvents; };
class MovementSystem {
    const MovementPorts ports_;
    std::map<PlayerId,uint64_t> skillSteps_;
    std::set<EntityId> admitted_;
  public:
    explicit MovementSystem(MovementPorts ports) : ports_(ports) {}
    CommandStatus execute(const ActorContext &, const MovementCommand &);
    DomainResult<bool> chargeStep(const ActorContext &,Vec target,float speed,int stopDistance);
    DomainResult<bool> leapStep(const ActorContext &,Vec target,float speed,std::optional<MovementCollisionRule> collision = {});
    void step(TickContext);
    DomainResult<> frameEvent(const FrameEvent &,TickContext);
    void suspend();
};
CommandStatus applyMovement(PlayerState &, const AreaState &, const MovementCommand &);
void advanceMovement(PlayerState &, const AreaState &, float seconds);
} // namespace d2x::server
