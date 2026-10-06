#pragma once
#include "client/actor_client.hpp"

namespace d2x {
class GameSession;
class LocalActorClient final : public IActorClient {
    GameSession &session_;
    EntityId actor_;

  public:
    explicit LocalActorClient(GameSession &session);
    ActorView controlledActor() const override;
    void control(ActorControlIntent intent) override;
    void move(MoveIntent intent) override;
    void stopMoving() override;
    void stopActions() override;
    void toggleRun() override;
    void respawn() override;
};
} // namespace d2x
