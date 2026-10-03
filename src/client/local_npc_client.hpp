#pragma once
#include "client/npc_client.hpp"

namespace d2x {
class GameSession;
class LocalNpcClient final : public INpcClient {
    GameSession &session_;
    mutable NpcConversationView cached_;
    mutable NpcSceneView scene_;
  public:
    explicit LocalNpcClient(GameSession &session) : session_(session) {}
    const NpcConversationView &read(EntityId npc) const override;
    const NpcSceneView &scene() const override;
    void submit(NpcIntent intent) override;
};
} // namespace d2x
