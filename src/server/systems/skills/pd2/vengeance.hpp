#pragma once
#include "server/runtime/contracts.hpp"
#include <memory>

namespace d2x {struct FrameEvent;}
namespace d2x::server::actions {struct SkillRelease;}
namespace d2x::server::skills {
struct Ports;
struct Pd2VengeancePulse;
struct Pd2VengeanceTimers;
inline constexpr uint32_t pd2VengeanceTimerHandler=0x50443256u;
DomainStatus pd2VengeanceAfterHit(const Ports &,actions::SkillRelease &,const ActorContext &,
    std::shared_ptr<Pd2VengeanceTimers> &);
DomainResult<> pd2VengeanceEvent(const Ports &,const FrameEvent &,uint64_t,
    std::shared_ptr<Pd2VengeanceTimers> &);
}
