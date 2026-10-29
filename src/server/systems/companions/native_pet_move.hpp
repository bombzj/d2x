#pragma once
#include "server/runtime/contracts.hpp"
#include "world/collision.hpp"
namespace d2x::server {struct PlayerState;namespace monsters {struct Actor;}}
namespace d2x::server::companions {
struct Ports;struct Companion;
struct NativePetMovement {
    int probeClass{-1};MovementCollisionRule probeCollision;
    int motion{-1},velocityBonus{},spacing{6};bool running{},suppressIdle{};
    bool registeredCount{};
};
bool followNativePet(const Ports &,Companion &,const PlayerState &,const monsters::Actor &,EntityId target,bool combat,uint64_t tick,const NativePetMovement &);
DomainResult<bool> followPd2Shadow(const Ports &,Companion &,const PlayerState &,const monsters::Actor &,EntityId target,uint64_t tick,const NativePetMovement &);
}
