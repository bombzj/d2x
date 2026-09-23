#include "gameplay/session/session.hpp"
#include <limits>

namespace d2x {
void GameSession::useItem(ItemHandle handle) {
    auto error = previewInventory(UseItem{handle});
    if (error != InventoryError::None) {
        simulation_.emit(InventoryRejected{handle.id, error});
        return;
    }
    auto code = inventory_.item(handle.id)->definition;
    if (code == "tsc") {
        auto &portal = simulation_.state_.portal;
        TownPortalState next{true, portal.revision + 1, region().definition.id,
                             state().player.pos, *townPortalArrival_};
        auto result = inventory_.consume(handle, 1, inventoryAccess());
        if (result) {
            cancelExit();
            cancelPickup();
            cancelInteraction();
            portal = next;
        }
        bool consumed = bool(result);
        publishInventory(std::move(result), handle.id);
        if (consumed)
            simulation_.emit(ItemUsed{handle.id, std::move(code)});
        return;
    }
    auto potion = *potionDefinition(code);
    auto result = inventory_.drink(handle, inventoryAccess());
    bool consumed = bool(result);
    publishInventory(std::move(result), handle.id);
    if (consumed) {
        simulation_.applyPotion(potion);
        simulation_.emit(ItemUsed{handle.id, std::move(code)});
    }
}
InventoryError GameSession::previewPortalScroll(ItemHandle handle) const {
    if (auto error = inventory_.checkHandle(handle); error != InventoryError::None)
        return error;
    const auto *item = inventory_.item(handle.id);
    auto location = std::get_if<ContainerLocation>(&item->location);
    const auto &player = state().player;
    if (!location || location->container != playerContainers_.backpack || player.dead ||
        player.hp <= 0 || region().definition.safe || player.leapTime > 0 || player.spinTime > 0 ||
        player.castTime > 0 || player.meleeTime > 0)
        return InventoryError::AccessDenied;
    if (!townPortalArrival_ || !portalResources_ || portalReach_ <= 0 ||
        int(region().definition.id) < 2 || int(region().definition.id) > 39 ||
        !map().grid.walkable(player.pos))
        return InventoryError::UnsupportedUse;
    if (state().portal.revision == std::numeric_limits<uint64_t>::max())
        return InventoryError::RevisionExhausted;
    return InventoryError::None;
}
std::optional<Vec> GameSession::portalPosition() const {
    const auto &portal = state().portal;
    if (!portal.active)
        return std::nullopt;
    if (region().definition.id == RegionId::Encampment)
        return portal.townPosition;
    if (region().definition.id == portal.field)
        return portal.fieldPosition;
    return std::nullopt;
}
void GameSession::beginPortal(uint64_t revision) {
    auto position = portalPosition();
    if (!position || state().portal.revision != revision || state().player.dead)
        return;
    cancelExit();
    cancelPickup();
    cancelInteraction();
    closeStorage();
    pendingPortal_ = revision;
    if ((state().player.pos - *position).length() > portalReach_)
        simulation_.execute(MoveTo{*position});
}
void GameSession::updatePortal() {
    if (!pendingPortal_)
        return;
    auto position = portalPosition();
    const auto &player = state().player;
    if (!position || *pendingPortal_ != state().portal.revision || player.dead) {
        cancelInteraction();
        return;
    }
    if (player.castTime > 0 || player.meleeTime > 0 || player.leapTime > 0 || player.spinTime > 0)
        return;
    if ((player.pos - *position).length() <= portalReach_ && map().grid.segment(player.pos, *position)) {
        bool returning = region().definition.id == RegionId::Encampment;
        auto destination = returning ? state().portal.field : RegionId::Encampment;
        Vec arrival = returning ? state().portal.fieldPosition : state().portal.townPosition;
        if (returning)
            simulation_.state_.portal.active = false;
        enter(destination, arrival);
    } else if (player.route.empty()) {
        cancelInteraction();
        simulation_.emit(InteractionFailed{{}, "Cannot reach the town portal."});
    }
}
void GameSession::useBeltColumn(int column) {
    auto belt = inventory_.container(playerContainers_.belt);
    if (!belt || column < 0 || column >= belt->spec.columns) {
        simulation_.emit(InventoryRejected{{}, InventoryError::InvalidRequest});
        return;
    }
    // A manually rearranged column may have a hole; use its lowest occupied cell.
    for (int row = 0; row < belt->spec.rows; ++row)
        if (auto item = inventory_.item(inventory_.itemAt(belt->id, {column, row}))) {
            useItem(item->handle());
            return;
        }
    simulation_.emit(PickupFailed{{}, "That belt column is empty."});
}
} // namespace d2x
