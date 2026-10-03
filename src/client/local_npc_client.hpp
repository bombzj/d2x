#pragma once
#include "client/npc_client.hpp"

namespace d2x {
class GameSession;
class LocalNpcClient final : public INpcClient {
    GameSession &session_;
    mutable NpcConversationView cached_;
    mutable NpcSceneView scene_;
    mutable ShopView shop_;
    mutable ShopOfferView inspectedOffer_;
    mutable uint64_t inspectedRevision_ = 0;
    mutable EntityId inspectedNpc_;
    mutable uint32_t inspectedSlot_ = 0;
    mutable bool inspectedGamble_ = false, inspectedValid_ = false;
    mutable HirelingView hireling_;
    mutable HirelingListView hirelings_;
  public:
    explicit LocalNpcClient(GameSession &session) : session_(session) {}
    const NpcConversationView &read(EntityId npc) const override;
    const NpcSceneView &scene() const override;
    const ShopView &shop(EntityId npc, bool gamble) const override;
    const ShopOfferView *inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const override;
    std::optional<unsigned> quote(EntityId npc, ItemHandle item, bool repair) const override;
    const HirelingView &hireling() const override;
    const HirelingListView &hirelings(EntityId npc) const override;
    void submit(NpcIntent intent) override;
};
} // namespace d2x
