#pragma once
#include "contracts/npc.hpp"
#include "gameplay/npc/intents.hpp"

namespace d2x {
class INpcClient {
  public:
    virtual ~INpcClient() = default;
    // Returned values belong to the bound player; each borrow ends on next read/destruction.
    virtual const NpcConversationView &read(EntityId npc) const = 0;
    virtual const NpcSceneView &scene() const = 0;
    virtual void submit(NpcIntent intent) = 0;
};
} // namespace d2x
