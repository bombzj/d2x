#pragma once
#include "contracts/actor.hpp"

namespace d2x {
// The adapter binds the controlled actor. UI never chooses an authority or owner.
class IActorClient {
  public:
    virtual ~IActorClient() = default;
    virtual ActorView controlledActor() const = 0;
    virtual bool control(ActorControlIntent intent) = 0;
    virtual bool move(MoveIntent intent) = 0;
    virtual void stopMoving() = 0;
    virtual void stopActions() = 0;
    virtual void toggleRun() = 0;
    virtual void respawn() = 0;
};
} // namespace d2x
