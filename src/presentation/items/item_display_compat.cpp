#include "presentation/scene_view.hpp"
#include "content/items/item_display.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/session/session.hpp"

namespace d2x {
// Ground loot and remaining world interactions retain the legacy item adapter.
std::string SceneView::itemName(const ItemInstance &item) const {
    return displayItemName(session_.content(), session_.inventory().catalog(), item);
}
void SceneView::drawItemIcon(const ItemInstance &item, Rectangle bounds, Color tint) const {
    drawItemArt(SceneAssets::itemArtKey(item), item.definition, bounds, tint);
}
void SceneView::drawItemTooltip(const ItemInstance &item, Vec anchor, std::optional<unsigned> price,
                                bool gamble, std::string_view priceLabel) const {
    if (gamble) {
        drawItemText({{itemName(item), ItemTextTone::Name}}, item.quality, anchor, price, priceLabel);
        return;
    }
    const auto &stats = session_.characterStats();
    ItemDisplayContext context{session_.state().player.character.level, stats.strength, stats.dexterity,
        session_.inventory().maximumDurability(item), {}};
    if (item.definition == "bkd") context.cainStones = session_.cainStoneSequence();
    auto display = describeInventoryItem(session_.content(), session_.inventory().catalog(), item, context);
    drawItemText(std::move(display.tooltip), item.quality, anchor, price, priceLabel);
}
} // namespace d2x
