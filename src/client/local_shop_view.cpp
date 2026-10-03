#include "client/local_npc_client.hpp"
#include "client/item_art.hpp"
#include "content/items/item_display.hpp"
#include "gameplay/session/session.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/npc/store.hpp"
#include "world/region.hpp"
#include <utility>
#include <algorithm>

namespace d2x {
const ShopView &LocalNpcClient::shop(EntityId npc, bool gamble) const {
    if (shop_.revision == session_.viewRevision() && shop_.npc == npc && shop_.gamble == gamble) return shop_;
    ShopView view;
    view.revision = session_.viewRevision(); view.npc = npc; view.gamble = gamble;
    const auto &player = session_.state().player;
    view.actor = player.id; view.bankGold = player.character.bankGold;
    const auto &content = session_.content();
    const auto *object = session_.object(npc);
    view.repairAvailable = !gamble && object && npcCanRepair(object->npcClass);
    if (const auto *stock = session_.vendorStock(npc, gamble)) {
        view.available = true;
        const auto &pages = content.tables.at("storepage");
        for (size_t index = 0; index < view.tabLabels.size(); ++index) {
            auto &label = view.tabLabels[index];
            label = pages.value(index == 2 ? 1 : index, "Store Page");
            if (label.ends_with(" Page")) label.resize(label.size() - 5);
        }
        const auto &inventory = session_.inventory();
        for (const auto &offer : *stock) {
            if (!gamble && session_.vendorOfferSold(npc, offer.slot)) continue;
            const auto *definition = inventory.catalog().find(gamble ? offer.displayCode : offer.code);
            if (!definition) continue;
            auto item = vendorItem(offer, content, gamble);
            ShopOfferView value;
            value.slot = offer.slot; value.storePage = offer.storePage;
            value.width = definition->width; value.height = definition->height;
            value.definition = item.definition; value.artKey = itemArtKey(item); value.quality = item.quality;
            value.price = session_.vendorPurchasePrice(npc, offer, gamble);
            value.name = displayItemName(content, inventory.catalog(), item);
            view.offers.push_back(std::move(value));
        }
    }
    shop_ = std::move(view);
    return shop_;
}
const ShopOfferView *LocalNpcClient::inspectShopOffer(EntityId npc, uint32_t slot, bool gamble) const {
    if (inspectedRevision_ == session_.viewRevision() && inspectedNpc_ == npc &&
        inspectedSlot_ == slot && inspectedGamble_ == gamble)
        return inspectedValid_ ? &inspectedOffer_ : nullptr;
    inspectedRevision_ = session_.viewRevision(); inspectedNpc_ = npc;
    inspectedSlot_ = slot; inspectedGamble_ = gamble; inspectedValid_ = false;
    const auto &stockView = shop(npc, gamble);
    const auto visible = std::find_if(stockView.offers.begin(), stockView.offers.end(),
        [slot](const auto &value) { return value.slot == slot; });
    const auto *stock = session_.vendorStock(npc, gamble);
    if (visible == stockView.offers.end() || !stock) return nullptr;
    const auto offer = std::find_if(stock->begin(), stock->end(),
        [slot](const auto &value) { return value.slot == slot; });
    if (offer == stock->end()) return nullptr;
    auto value = *visible;
    if (gamble) value.tooltip.push_back({value.name, ItemTextTone::Name});
    else {
        const auto &inventory = session_.inventory();
        const auto &content = session_.content();
        auto item = vendorItem(*offer, content);
        const auto &stats = session_.characterStats();
        ItemDisplayContext context{session_.state().player.character.level, stats.strength, stats.dexterity,
            inventory.maximumDurability(item), {}};
        auto display = describeInventoryItem(content, inventory.catalog(), item, context);
        value.tooltip = std::move(display.tooltip);
    }
    inspectedOffer_ = std::move(value); inspectedValid_ = true;
    return &inspectedOffer_;
}
std::optional<unsigned> LocalNpcClient::quote(EntityId npc, ItemHandle item, bool repair) const {
    return repair ? session_.vendorRepairQuote(npc, item) : session_.vendorSaleQuote(npc, item);
}
} // namespace d2x
