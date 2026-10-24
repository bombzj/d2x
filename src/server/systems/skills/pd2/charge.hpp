#pragma once
#include "server/runtime/contracts.hpp"

namespace d2x::server::actions {struct SkillRelease;}
namespace d2x::server::skills {
struct Ports;
DomainStatus pd2ChargeBeforeHit(const Ports &,actions::SkillRelease &,const ActorContext &);
}
