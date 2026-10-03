#pragma once
#include "client/actor_client.hpp"

namespace d2x {
class GameSession;
class LocalActorClient final : public IActorClient {
    GameSession &session_;

  public:
    explicit LocalActorClient(GameSession &session);
    ActorView controlledActor() const override;
    void move(MoveIntent intent) override;
    void stopMoving() override;
    void toggleRun() override;
};
} // namespace d2x
