#include "gameplay/session/session.hpp"
#include <type_traits>

namespace d2x {
bool GameSession::inventorySourceAllowed(EntityId id) const {
    const auto *item = inventory_.item(id);
    if (item)
        if (auto ground = std::get_if<GroundLocation>(&item->location))
            return inventoryDestinationAllowed(*ground);
    return true; // Missing IDs and stale revisions are reported by InventoryService.
}
InventoryError GameSession::previewInventory(const GameCommand &command) const {
    return std::visit(
        [&](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SplitStack>) {
                EntityId source;
                if constexpr (std::is_same_v<T, MoveItem>)
                    source = intent.item.id;
                else
                    source = intent.source.id;
                if (!inventorySourceAllowed(source))
                    return InventoryError::AccessDenied;
                if (!inventoryDestinationAllowed(intent.destination))
                    return InventoryError::InvalidLocation;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, SwapItems> || std::is_same_v<T, MergeStacks>) {
                EntityId first, second;
                if constexpr (std::is_same_v<T, SwapItems>) {
                    first = intent.first.id;
                    second = intent.second.id;
                } else {
                    first = intent.source.id;
                    second = intent.target.id;
                }
                if (!inventorySourceAllowed(first) || !inventorySourceAllowed(second))
                    return InventoryError::AccessDenied;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, TransferItem>) {
                if (!inventorySourceAllowed(intent.item.id))
                    return InventoryError::AccessDenied;
                return inventory_.preview(intent, inventoryAccess());
            } else if constexpr (std::is_same_v<T, EquipBelt>)
                return inventory_.preview(intent, playerContainers_, inventoryAccess());
            else if constexpr (std::is_same_v<T, UseItem>) {
                auto error = inventory_.previewDrink(intent.item, inventoryAccess());
                if (error != InventoryError::None)
                    return error;
                return potionDefinition(inventory_.item(intent.item.id)->definition)
                           ? InventoryError::None
                           : InventoryError::UnsupportedUse;
            } else
                return InventoryError::InvalidRequest;
        },
        command);
}
void GameSession::executeInventory(const GameCommand &command) {
    std::visit(
        [&](const auto &intent) {
            using T = std::decay_t<decltype(intent)>;
            if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, SwapItems> ||
                          std::is_same_v<T, SplitStack> || std::is_same_v<T, MergeStacks> ||
                          std::is_same_v<T, EquipBelt> || std::is_same_v<T, TransferItem>) {
                EntityId requested;
                if constexpr (std::is_same_v<T, MoveItem> || std::is_same_v<T, EquipBelt> ||
                              std::is_same_v<T, TransferItem>)
                    requested = intent.item.id;
                else if constexpr (std::is_same_v<T, SwapItems>)
                    requested = intent.first.id;
                else
                    requested = intent.source.id;
                auto error = previewInventory(command);
                if (error != InventoryError::None) {
                    simulation_.emit(InventoryRejected{requested, error});
                    return;
                }
                if constexpr (std::is_same_v<T, MoveItem>)
                    publishInventory(inventory_.move(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, SwapItems>)
                    publishInventory(inventory_.swap(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, SplitStack>)
                    publishInventory(inventory_.split(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, TransferItem>)
                    publishInventory(inventory_.transfer(intent, inventoryAccess()), requested);
                else if constexpr (std::is_same_v<T, EquipBelt>) {
                    auto result = inventory_.equipBelt(intent, playerContainers_, inventoryAccess());
                    bool applied = bool(result);
                    publishInventory(std::move(result), requested);
                    if (applied)
                        simulation_.emit(BeltEquipped{});
                } else
                    publishInventory(inventory_.merge(intent, inventoryAccess()), requested);
            }
        },
        command);
}
std::optional<GroundLocation> GameSession::dropLocation() const {
    const auto &player = state().player;
    if (player.dead)
        return std::nullopt;
    Vec position = map().grid.nearest(player.pos + player.look.unit());
    if (!map().grid.segment(player.pos, position) || (position - player.pos).length() > 4)
        position = player.pos;
    GroundLocation location{region().definition.id, position};
    return inventoryDestinationAllowed(location) ? std::optional{location} : std::nullopt;
}
} // namespace d2x
