#include "gameplay/session/session.hpp"
#include <algorithm>

namespace d2x {
const std::vector<VendorOffer> *GameSession::vendorStock(EntityId npc) const {
    auto found = vendorStocks_.find(npc);
    return found == vendorStocks_.end() ? nullptr : &found->second;
}
bool GameSession::vendorOfferSold(EntityId npc, uint32_t slot) const {
    auto found = soldVendorOffers_.find(npc);
    return found != soldVendorOffers_.end() && found->second.contains(slot);
}
void GameSession::buyVendorItem(EntityId npc, uint32_t slot) {
    const auto *target = object(npc);
    const auto *stock = vendorStock(npc);
    // Opening Trade already checked the NPC interaction range. NPCs may wander
    // during a transaction, so distance is not rechecked on each purchase.
    if (!target || !stock || engagedNpc_ != npc || state().player.dead ||
        !region().definition.safe) {
        simulation_.emit(InteractionFailed{npc, "Vendor is unavailable in this town."});
        return;
    }
    auto found = std::find_if(stock->begin(), stock->end(),
                              [slot](const VendorOffer &offer) { return offer.slot == slot; });
    if (found == stock->end() || (!found->permanent && vendorOfferSold(npc, slot))) {
        simulation_.emit(InteractionFailed{npc, "That vendor item is no longer available."});
        return;
    }
    if (state().player.gold < found->price) {
        simulation_.emit(InteractionFailed{npc, "Not enough gold to buy that item."});
        return;
    }
    auto sold = soldVendorOffers_;
    if (!found->permanent)
        sold[npc].insert(slot);
    auto creationRandom = inventory_.state_.creationRandom;
    auto purchased = inventory_.createItem(found->code, found->quantity,
                                            AutoPlace{playerContainers_.backpack}, found->level);
    if (!purchased) {
        simulation_.emit(InteractionFailed{npc, inventoryErrorText(purchased.error)});
        return;
    }
    auto item = purchased.item;
    inventory_.state_.items.at(item).defense = found->defense;
    inventory_.state_.creationRandom = creationRandom;
    simulation_.state_.player.gold -= found->price;
    soldVendorOffers_.swap(sold);
    publishInventory(std::move(purchased), {});
    simulation_.emit(VendorItemBought{npc, item, slot, found->price});
}
} // namespace d2x
