#pragma once
#include "server/runtime/contracts.hpp"
#include <optional>
#include <cstdint>
#include <functional>
namespace d2x::server {
class PlayerStore;class AreaStore;struct PlayerState;
namespace monsters {class System;struct Actor;}
namespace social {class System;}
namespace ai {
struct NativeAiTarget {UnitTarget unit;Vec position;int size{},distance{};};
struct NativeAiTargetPorts {monsters::System &monsters;const PlayerStore &players;const AreaStore &areas;const social::System &social;};
struct NativeBossTarget {std::optional<NativeAiTarget> target;int score{},count{};};
NativeBossTarget acquireNativeBossTarget(const NativeAiTargetPorts &,const monsters::Actor &,
    const std::function<int(EntityId)> &score);
std::optional<NativeAiTarget> acquireNativeOwnerTarget(const NativeAiTargetPorts &,const PlayerState &);
// A supplied candidate owns Confuse's selector draw; the caller publishes it
// together with its remaining AI draws. Other callers advance the unit directly.
std::optional<NativeAiTarget> acquireNativeAiTarget(const NativeAiTargetPorts &,const monsters::Actor &,TickContext,bool missileRay,bool requireSight,int searchDistance=0,uint64_t *borrowedRandom=nullptr);
}
}
