#pragma once
#include "contracts/npc.hpp"
#include "contracts/shop.hpp"
#include "contracts/hireling.hpp"
#include "gameplay/npc/intents.hpp"

namespace d2x {
class INpcClient {
  public:
    virtual ~INpcClient() = default;
    // Returned values belong to the bound player; each borrow ends on next read/destruction.
    virtual const NpcConversationView &read(EntityId npc) const = 0;
    virtual const NpcSceneView &scene() const = 0;
    virtual const ShopView &shop(EntityId npc, bool gamble) const = 0;
    virtual const ShopOfferView *inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const = 0;
    virtual std::optional<unsigned> quote(EntityId npc, ItemHandle item, bool repair) const = 0;
    virtual const HirelingView &hireling() const = 0;
    virtual const HirelingListView &hirelings(EntityId npc) const = 0;
    virtual void submit(NpcIntent intent) = 0;
};
} // namespace d2x
