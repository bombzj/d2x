#pragma once
#include "server/game_commands.hpp"
#include "server/runtime/contracts.hpp"
#include "gameplay/events/frame_queue.hpp"
#include "world/collision.hpp"
#include <map>
#include <set>

namespace d2x {struct Grid;}
namespace d2x::server {
class PlayerStore;
class AreaStore;
struct AreaState;
struct PlayerState;
namespace travel { class System; }
namespace monsters { class System; }
namespace effects { class System; }
namespace items { class System; }
namespace objects { class System; }
namespace spatial { class System; }
struct ActorContext;
struct TickContext;
inline constexpr uint32_t movementTimerHandler=0x4d4f5645;
struct MovementPorts { PlayerStore &players; AreaStore &areas; travel::System &travel; const monsters::System &monsters; effects::System &effects; const items::System &items; const objects::System &objects; FrameEventQueue &frameEvents; const spatial::System &spatial; };
class MovementSystem {
    const MovementPorts ports_;
    CommandStatus apply(PlayerState &,const AreaState &,const MovementCommand &);
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
CommandStatus applyMovement(PlayerState &, const AreaState &, const MovementCommand &,const Grid &);
} // namespace d2x::server
