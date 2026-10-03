#include "gameplay/simulation/simulation.hpp"
#include "gameplay/session/session_impl.hpp"
#include "content/items/item_pricing.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace d2x {
std::vector<int> GameSessionImpl::vendorQuestFactors(const VendorDefinition &vendor, bool repair, bool sale) const {
    // The values are D2MOO QuestStateFlag IDs, not the quest log's display order.
    auto pendingOrRewarded = [&](int flag) {
        switch (flag) {
        case 1: return quest(ActOneQuest::DenOfEvil).stage >= uint32_t(DenStage::Cleared);
        case 2: return quest(ActOneQuest::SistersBurialGrounds).stage >= uint32_t(BurialStage::BloodRavenSlain);
        case 3: return quest(ActOneQuest::ToolsOfTheTrade).stage >= uint32_t(ToolsStage::RewardReady);
        case 4: return quest(ActOneQuest::SearchForCain).stage >= uint32_t(CainStage::Rescued);
        case 5: return quest(ActOneQuest::ForgottenTower).stage >= uint32_t(TowerStage::CountessSlain);
        case 6: return quest(ActOneQuest::SistersToTheSlaughter).stage >= uint32_t(SlaughterStage::AndarielSlain);
        case 9: return quest(QuestId::RadamentsLair).stage >= uint32_t(RadamentStage::Slain);
        default: return false;
        }
    };
    std::vector<int> result;
    for (const auto &price : vendor.questPrices)
        if (pendingOrRewarded(price.flag)) result.push_back(repair ? price.repair : sale ? price.buy : price.sell);
    return result;
}
unsigned GameSessionImpl::vendorPurchasePrice(EntityId npc, const VendorOffer &offer, bool gamble) const {
    const int reduce = std::clamp(characterStats().combat.reducedPrices, 0, 99);
    if (gamble) return std::max(1u, offer.price - unsigned(uint64_t(offer.price) * reduce / 100));
    const auto *target = object(npc);
    if (!target) return offer.price;
    auto vendor = content_.vendors.find(target->npcClass);
    if (vendor == content_.vendors.end()) return offer.price;
    const auto factors = vendorQuestFactors(vendor->second, false);
    return itemTradePrice(content_, vendorItem(offer, content_), vendor->second, false, factors, reduce)
        .value_or(offer.price);
}
std::optional<unsigned> GameSessionImpl::vendorRepairQuote(EntityId npc, ItemHandle handle) const {
    const auto *target = object(npc);
    const auto *item = inventory_.item(handle.id);
    if (!target || !npcCanRepair(target->npcClass) || !item || item->revision != handle.revision)
        return {};
    const auto location = std::get_if<ContainerLocation>(&item->location);
    if (!location || (location->container != playerContainers_.backpack &&
        location->container != playerContainers_.equipment && location->container != playerContainers_.beltEquipment)) return {};
    auto vendor = content_.vendors.find(target->npcClass);
    if (vendor == content_.vendors.end()) return {};
    const auto factors = vendorQuestFactors(vendor->second, true);
    return itemTradePrice(content_, *item, vendor->second, true, factors, characterStats().combat.reducedPrices);
}
std::optional<unsigned> GameSessionImpl::vendorSaleQuote(EntityId npc, ItemHandle handle) const {
    const auto *target = object(npc);
    const auto *item = inventory_.item(handle.id);
    if (!target || !vendorStock(npc) || engagedNpc_ != npc || state().player.dead ||
        !region().definition.safe || !item || item->revision != handle.revision ||
        item->revision == std::numeric_limits<uint64_t>::max()) return {};
    const auto *location = std::get_if<ContainerLocation>(&item->location);
    if (!location || (location->container != playerContainers_.backpack &&
                      location->container != playerContainers_.equipment &&
                      location->container != playerContainers_.beltEquipment)) return {};
    const auto *definition = inventory_.catalog().find(item->definition);
    if (!definition || content_.tables.at(definition->base.sourceTable)
                           .number(definition->base.sourceRow, "quest").value_or(0)) return {};
    auto vendor = content_.vendors.find(target->npcClass);
    if (vendor == content_.vendors.end()) return {};
    const auto factors = vendorQuestFactors(vendor->second, false, true);
    return itemTradePrice(content_, *item, vendor->second, false, factors, 0, true,
                          state().population.difficulty);
}
void GameSessionImpl::sellVendorItem(const SellVendorItem &command) {
    const auto quote = vendorSaleQuote(command.vendor, command.item);
    if (!quote) {
        simulation_->emit(InteractionFailed{command.vendor, "That item cannot be sold here."});
        return;
    }
    auto &player = simulation_->state_.player;
    const unsigned walletLimit = unsigned(player.level) * 10000u;
    if (*quote > walletLimit - player.gold) {
        simulation_->emit(InteractionFailed{command.vendor, "Make room for the sale gold first."});
        return;
    }
    const auto item = inventory_.state_.items.at(command.item.id);
    InventoryResult result;
    result.item = item.id;
    result.changes.push_back({item.id, item.revision + 1, ItemChangeKind::Removed,
                              item.location, std::nullopt, 0});
    inventory_.state_.items.erase(item.id);
    player.gold += *quote;
    publishInventory(std::move(result), {});
    simulation_->emit(VendorItemSold{command.vendor, item.id, *quote});
}
void GameSessionImpl::repairVendorItem(const RepairVendorItem &command) {
    const auto quote = vendorRepairQuote(command.npc, command.item);
    if (engagedNpc_ != command.npc || state().player.dead || !region().definition.safe || !quote) {
        simulation_->emit(InteractionFailed{command.npc, "That item cannot be repaired here."});
        return;
    }
    if (!*quote) return;
    auto &player = simulation_->state_.player;
    if (uint64_t(player.gold) + player.bankGold < *quote) {
        simulation_->emit(InteractionFailed{command.npc, "Not enough gold to repair that item."});
        return;
    }
    auto &item = inventory_.state_.items.at(command.item.id);
    if (item.revision == std::numeric_limits<uint64_t>::max()) return;
    const auto &definition = *inventory_.catalog().find(item.definition);
    InventoryResult result;
    result.item = item.id;
    result.changes.push_back({item.id, item.revision + 1, ItemChangeKind::DurabilityChanged,
        item.location, item.location, item.quantity});
    if (definition.equipment.throwable && definition.equipment.repairable)
        result.changes.push_back({item.id, item.revision + 1, ItemChangeKind::QuantityChanged,
            item.location, item.location, inventory_.maximumStack(item)});
    const unsigned walletPaid = std::min(player.gold, *quote);
    player.gold -= walletPaid;
    player.bankGold -= *quote - walletPaid;
    item.durability = inventory_.maximumDurability(item);
    if (definition.equipment.throwable && definition.equipment.repairable)
        item.quantity = inventory_.maximumStack(item);
    ++item.revision;
    publishInventory(std::move(result), {});
}
const std::vector<VendorOffer> *GameSessionImpl::vendorStock(EntityId npc, bool gamble) const {
    const auto &stocks = gamble ? gambleStocks_ : vendorStocks_;
    auto found = stocks.find(npc);
    return found == stocks.end() ? nullptr : &found->second;
}
void GameSessionImpl::openGamble(EntityId npc) {
    const auto *target = object(npc);
    if (!target || !npcCanGamble(target->npcClass) || engagedNpc_ != npc ||
        state().player.dead || !region().definition.safe) {
        simulation_->emit(InteractionFailed{npc, "Gambling is unavailable."});
        return;
    }
    auto random = inventory_.state_.creationRandom;
    try {
        auto stock = planGambleStock(content_, unsigned(state().player.level),
            state().population.difficulty, random, loot_.usedUniques(), characterDefinition_.code);
        gambleStocks_[npc] = std::move(stock);
        inventory_.state_.creationRandom = random;
        simulation_->emit(GambleStockOpened{npc});
    } catch (const std::runtime_error &error) {
        simulation_->emit(InteractionFailed{npc, error.what()});
    }
}
bool GameSessionImpl::vendorOfferSold(EntityId npc, uint32_t slot) const {
    auto found = soldVendorOffers_.find(npc);
    return found != soldVendorOffers_.end() && found->second.contains(slot);
}
void GameSessionImpl::buyVendorItem(EntityId npc, uint32_t slot, bool gamble) {
    const auto *target = object(npc);
    const auto *stock = vendorStock(npc, gamble);
    // Opening Trade already checked the NPC interaction range. NPCs may wander
    // during a transaction, so distance is not rechecked on each purchase.
    if (!target || !stock || engagedNpc_ != npc || state().player.dead ||
        !region().definition.safe || (gamble && !npcCanGamble(target->npcClass))) {
        simulation_->emit(InteractionFailed{npc, "Vendor is unavailable in this town."});
        return;
    }
    auto found = std::find_if(stock->begin(), stock->end(),
                              [slot](const VendorOffer &offer) { return offer.slot == slot; });
    if (found == stock->end() || (!gamble && !found->permanent && vendorOfferSold(npc, slot))) {
        simulation_->emit(InteractionFailed{npc, "That vendor item is no longer available."});
        return;
    }
    const unsigned paid = vendorPurchasePrice(npc, *found, gamble);
    if (uint64_t(state().player.gold) + state().player.bankGold < paid) {
        simulation_->emit(InteractionFailed{npc, "Not enough gold to buy that item."});
        return;
    }
    auto sold = soldVendorOffers_;
    if (!gamble && !found->permanent)
        sold[npc].insert(slot);
    auto purchased = inventory_.createItem(found->code, found->quantity,
                                            AutoPlace{playerContainers_.backpack}, found->level,
                                            found->generation);
    if (!purchased) {
        simulation_->emit(InteractionFailed{npc, inventoryErrorText(purchased.error)});
        return;
    }
    auto item = purchased.item;
    inventory_.state_.items.at(item).defense = found->defense;
    inventory_.state_.items.at(item).identified = true;
    auto &player = simulation_->state_.player;
    unsigned walletPaid = std::min(player.gold, paid);
    player.gold -= walletPaid;
    player.bankGold -= paid - walletPaid;
    soldVendorOffers_.swap(sold);
    if (gamble) {
        if (found->generation.quality == ItemQuality::Unique && found->generation.specialRow >= 0)
            loot_.recordUnique(uint32_t(found->generation.specialRow));
        std::erase_if(gambleStocks_.at(npc), [slot](const auto &offer) { return offer.slot == slot; });
    }
    publishInventory(std::move(purchased), {});
    simulation_->emit(VendorItemBought{npc, item, slot, paid});
}
} // namespace d2x
