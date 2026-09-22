#include "gameplay/session/session.hpp"
#include <algorithm>
#include <stdexcept>

namespace d2x {
void GameSession::settleDeaths() {
    // Publishing item events can reallocate the event vector; copy deaths before appending anything.
    std::vector<EnemyDied> deaths;
    for (const auto &event : events())
        if (auto death = std::get_if<EnemyDied>(&event))
            deaths.push_back(*death);
    for (const auto &death : deaths) {
        auto drops = loot_.settle({death.victim, death.identity, death.region, death.difficulty});
        spawnLoot(drops, death.region, death.position);
    }
}
void GameSession::spawnLoot(std::span<const LootDrop> drops, RegionId id, Vec origin) {
    auto region = std::find_if(regions_.begin(), regions_.end(),
                               [id](const Region &r) { return r.definition.id == id; });
    if (region == regions_.end())
        throw std::logic_error("Loot references an unknown region");
    const auto &grid = region->map.grid;
    origin = grid.nearest(origin);
    if (!grid.walkable(origin))
        throw std::logic_error("Loot origin has no walkable ground");
    for (const auto &drop : drops) {
        Vec position = grid.nearest(origin + drop.offset);
        if (!grid.segment(origin, position))
            position = origin;
        auto result = inventory_.createItem(drop.code, drop.quantity, GroundLocation{id, position});
        if (!result)
            throw std::logic_error("Invalid loot definition or placement");
        publishInventory(std::move(result), {});
    }
}
void GameSession::cancelPickup() {
    if (pickup_.id)
        simulation_.stopWalking();
    pickup_ = {};
}
void GameSession::beginPickup(ItemHandle handle) {
    if (pickup_.id == handle.id && pickup_.revision == handle.revision)
        return;
    cancelPickup();
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        simulation_.emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    const auto &player = state().player;
    if (player.dead || !ground || ground->region != region().definition.id) {
        simulation_.emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    if (player.leapTime > 0 || player.spinTime > 0) {
        simulation_.emit(PickupFailed{handle.id, "Finish your skill before picking up an item."});
        return;
    }
    simulation_.stopWalking();
    simulation_.execute(MoveTo{ground->position});
    if (player.route.empty()) {
        simulation_.emit(PickupFailed{handle.id, "Cannot reach that item."});
        return;
    }
    pickup_ = handle;
}
void GameSession::updatePickup() {
    if (!pickup_.id)
        return;
    const auto &player = state().player;
    if (player.dead) {
        cancelPickup();
        return;
    }
    const auto handle = pickup_;
    const auto *item = inventory_.item(handle.id);
    if (!item || item->revision != handle.revision) {
        cancelPickup();
        simulation_.emit(
            InventoryRejected{handle.id, item ? InventoryError::SourceChanged : InventoryError::UnknownItem});
        return;
    }
    const auto *ground = std::get_if<GroundLocation>(&item->location);
    if (!ground || ground->region != region().definition.id) {
        cancelPickup();
        simulation_.emit(InventoryRejected{handle.id, InventoryError::AccessDenied});
        return;
    }
    if (player.castTime > 0 || player.meleeTime > 0 || player.leapTime > 0 || player.spinTime > 0)
        return;
    auto access = inventoryAccess();
    // Pickup is deliberately closer than generic container access; walls also block the hand-off.
    access.reach = 1.8f;
    if ((ground->position - player.pos).length() <= access.reach &&
        map().grid.segment(player.pos, ground->position)) {
        auto definition = item->definition;
        unsigned quantity = item->quantity;
        auto beltSlot = inventory_.beltSpace(playerContainers_.belt, definition, true);
        auto result =
            beltSlot ? inventory_.move(MoveItem{handle, ContainerLocation{playerContainers_.belt, *beltSlot}},
                                       access)
                     : inventory_.collect(handle, playerContainers_.backpack, access);
        bool collected = bool(result);
        cancelPickup();
        publishInventory(std::move(result), handle.id);
        if (collected)
            simulation_.emit(ItemPickedUp{handle.id, std::move(definition), quantity});
    } else if (player.route.empty()) {
        cancelPickup();
        simulation_.emit(PickupFailed{handle.id, "Cannot reach that item."});
    }
}
} // namespace d2x
