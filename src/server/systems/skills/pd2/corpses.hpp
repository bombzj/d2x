#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x {struct FrameEvent;}
namespace d2x::server::actions {struct SkillRelease;}
namespace d2x::server::skills {
struct Ports;
inline constexpr uint32_t pd2CorpseTimerHandler=0x50443243u;
DomainStatus pd2Desecrate(const Ports &,actions::SkillRelease &,const ActorContext &,Vec);
DomainResult<> pd2CorpseEvent(const Ports &,const FrameEvent &,uint64_t);
}
