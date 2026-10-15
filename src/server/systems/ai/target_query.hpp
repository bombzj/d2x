#pragma once
#include "server/runtime/contracts.hpp"
#include <optional>
namespace d2x::server {
class PlayerStore;class AreaStore;struct PlayerState;
namespace monsters {class System;struct Actor;}
namespace social {class System;}
namespace ai {
struct NativeAiTarget {UnitTarget unit;Vec position;int size{},distance{};};
struct NativeAiTargetPorts {monsters::System &monsters;const PlayerStore &players;const AreaStore &areas;const social::System &social;};
std::optional<NativeAiTarget> acquireNativeOwnerTarget(const NativeAiTargetPorts &,const PlayerState &);
std::optional<NativeAiTarget> acquireNativeAiTarget(const NativeAiTargetPorts &,const monsters::Actor &,TickContext,bool missileRay,bool requireSight,int searchDistance=0);
}
}
