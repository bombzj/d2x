#include "client/local_inventory_client.hpp"
#include "client/item_art.hpp"
#include "content/classic_data.hpp"
#include "content/items/item_display.hpp"
#include "gameplay/items/inventory.hpp"
#include "gameplay/model/state.hpp"
#include "gameplay/session/session.hpp"
#include <utility>
#include "world/region.hpp"

namespace d2x {
const InventoryView &LocalInventoryClient::read() const {
    if (cached_.revision == session_.viewRevision()) return cached_;
    const auto &inventory = session_.inventory();
    const auto &content = session_.content();
    const auto &player = session_.state().player;
    const auto &stats = session_.characterStats();
    InventoryView view;
    view.revision = session_.viewRevision();
    view.containers = session_.playerContainers();
    view.storage = session_.storage().container;
    view.weaponSet = player.character.weaponSet;
    view.gold = player.character.gold;
    view.bankGold = player.character.bankGold;
    view.walletLimit = unsigned(player.character.level) * 10000u;
    view.bankGoldLimit = session_.bankGoldLimit();
    view.groundGoldLimit = session_.groundGoldLimit();
    view.dead = player.actions.dead;
    auto layout = [](const auto &value) {
        return InventoryLayoutView{value.columns, value.rows, value.left, value.top, value.cellSize, value.expansion};
    };
    view.stashLayout = layout(content.stashLayout);
    view.cubeLayout = layout(content.cubeLayout);
    view.hirelingSlots = content.hirelingLayout.slots;
    view.cubeCode = content.cubeCode;
    view.staffRecipeOutput = content.staffRecipe.output;
    view.dropLocation = session_.dropLocation();
    const auto &owned = view.containers;
    // Player containers, authorized storage and active ground items share the UI snapshot.
    std::vector<EntityId> visibleItems;
    for (auto id : {owned.backpack, owned.belt, owned.stash, owned.beltEquipment,
                   owned.equipment, owned.cube, owned.hirelingEquipment, owned.cursor, view.storage}) {
        const auto *container = inventory.container(id);
        if (!container || view.containerViews.contains(id)) continue;
        view.containerViews.emplace(id, InventoryContainerView{id, container->spec.kind, container->spec.columns, container->spec.rows});
        for (auto itemId : inventory.contents(id)) visibleItems.push_back(itemId);
    }
    for (auto itemId : inventory.groundItems(session_.region().definition.id)) {
        const auto &item=*inventory.item(itemId);
        if (session_.active(std::get<GroundLocation>(item.location).position)) visibleItems.push_back(itemId);
    }
    view.pickupTarget=session_.pickupTarget();
    for (auto itemId:visibleItems) {
            const auto &item = *inventory.item(itemId);
            const auto &definition = *inventory.catalog().find(item.definition);
            if (!view.definitions.contains(item.definition)) {
                InventoryDefinitionView value;
                value.targetCursor = definition.targetCursor;
                value.code = definition.code;
                value.name = definition.name;
                value.bookScroll = definition.bookScroll;
                value.width = definition.width;
                value.height = definition.height;
                value.beltRows = definition.beltRows;
                value.maxStack = definition.maxStack;
                value.bookCapacity = definition.bookCapacity;
                value.beltAllowed = definition.beltAllowed;
                value.opensCube = definition.opensCube;
                value.twoHanded = definition.equipment.twoHanded;
                value.socketFiller = definition.equipment.isType("sock");
                value.identifySource = content.isIdentifyScroll(item.definition) || content.isIdentifyScroll(definition.bookScroll);
                for (size_t slot = 0; slot < value.slots.size(); ++slot)
                    value.slots[slot] = definition.equipment.fits(EquipmentSlot(slot));
                view.definitions.emplace(item.definition, std::move(value));
            }
            ItemDisplayContext context{player.character.level, stats.strength, stats.dexterity,
                                       inventory.maximumDurability(item), {}};
            if (item.definition == "bkd") context.cainStones = session_.cainStoneSequence();
            auto display = describeInventoryItem(content, inventory.catalog(), item, context);
            view.items.emplace(item.id, InventoryItemView{item.id, item.revision, item.definition,
                itemArtKey(item), std::move(display.name), item.location, item.quality, item.identified,
                item.quantity, item.durability, item.charges, std::move(display.tooltip),item.nativeFlags,item.sockets,item.runewordRow>=0,{}});
    }
    cached_ = std::move(view);
    return cached_;
}
InventoryError LocalInventoryClient::preview(const InventoryIntent &intent) const {
    return std::visit([&](const auto &value) { return session_.previewInventory(GameCommand{value}); }, intent);
}
void LocalInventoryClient::submit(InventoryIntent intent) {
    std::visit([&](auto value) { session_.submit(GameCommand{std::move(value)}); }, std::move(intent));
}
std::optional<Cell> LocalInventoryClient::beltSpace(std::string_view code) const {
    return session_.inventory().beltSpace(session_.playerContainers().belt, code, false);
}
} // namespace d2x
