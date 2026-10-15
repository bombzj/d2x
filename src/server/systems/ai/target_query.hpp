#pragma once
#include "server/runtime/contracts.hpp"
#include <optional>
namespace d2x::server {
class PlayerStore;class AreaStore;
namespace monsters {class System;struct Actor;}
namespace social {class System;}
namespace ai {
struct NativeAiTarget {UnitTarget unit;Vec position;int size{},distance{};};
struct NativeAiTargetPorts {monsters::System &monsters;const PlayerStore &players;const AreaStore &areas;const social::System &social;};
std::optional<NativeAiTarget> acquireNativeAiTarget(const NativeAiTargetPorts &,const monsters::Actor &,TickContext,bool missileRay,bool requireSight);
}
}
