#include "inventory.hpp"
#include <algorithm>

namespace d2x {
InventoryResult InventoryService::planTransfer(const TransferItem &command,
                                               const InventoryAccess &access) const {
    auto handle = command.item;
    auto backpack = command.destination;
    auto failure = [](InventoryError error) {
        InventoryResult result;
        result.error = error;
        return result;
    };
    if (auto error = checkHandle(handle); error != InventoryError::None)
        return failure(error);
    const auto &source = state_.items.at(handle.id);
    if (auto at = std::get_if<ContainerLocation>(&source.location); at && at->container == backpack)
        return failure(InventoryError::InvalidRequest);
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return failure(error);
    auto destination = container(backpack);
    if (!destination ||
        (destination->spec.kind != ContainerKind::Backpack &&
         destination->spec.kind != ContainerKind::Stash && destination->spec.kind != ContainerKind::Chest))
        return failure(InventoryError::UnknownContainer);
    if (auto error = checkDestinationAccess(AutoPlace{backpack}, access); error != InventoryError::None)
        return failure(error);
    const auto &definition = *catalog_.find(source.definition);
    if (definition.equipment.isType("gold"))
        return failure(InventoryError::RestrictedItem);
    unsigned remaining = source.quantity;
    std::vector<std::pair<EntityId, unsigned>> merges;
    if (definition.maxStack > 1 && source.quality == ItemQuality::Normal)
        for (auto id : contents(backpack)) {
            const auto &target = state_.items.at(id);
            if (target.definition != source.definition || target.quality != source.quality ||
                target.level != source.level || target.durability != source.durability ||
                target.quantity == definition.maxStack)
                continue;
            if (auto error = checkHandle(target.handle()); error != InventoryError::None)
                return failure(error);
            unsigned amount = std::min(remaining, definition.maxStack - target.quantity);
            merges.emplace_back(id, amount);
            remaining -= amount;
            if (!remaining)
                break;
        }
    std::optional<Cell> slot;
    if (remaining) {
        slot = findSpace(backpack, source.definition);
        if (!slot)
            return failure(InventoryError::NoSpace);
    }
    InventoryResult result;
    result.item = source.id;
    result.transferred = source.quantity;
    result.changes.reserve(merges.size() + 1);
    for (auto [id, amount] : merges) {
        const auto &target = state_.items.at(id);
        result.changes.push_back({id, target.revision + 1, ItemChangeKind::QuantityChanged, target.location,
                                  target.location, target.quantity + amount});
    }
    std::optional<ItemLocation> after;
    if (remaining)
        after = ContainerLocation{backpack, *slot};
    result.changes.push_back({source.id, source.revision + 1,
                              remaining ? ItemChangeKind::Moved : ItemChangeKind::Removed, source.location,
                              after, remaining});
    return result;
}
InventoryError InventoryService::preview(const TransferItem &command, const InventoryAccess &access) const {
    return planTransfer(command, access).error;
}
InventoryResult InventoryService::transfer(const TransferItem &command, const InventoryAccess &access) {
    auto result = planTransfer(command, access);
    if (!result)
        return result;
    // Planning allocated the complete change list; committing needs no new items or IDs.
    for (const auto &change : result.changes) {
        if (!change.after)
            state_.items.erase(change.item);
        else {
            auto &item = state_.items.at(change.item);
            item.location = *change.after;
            item.quantity = change.quantity;
            item.revision = change.revision;
        }
    }
    return result;
}
InventoryResult InventoryService::collect(ItemHandle handle, EntityId backpack,
                                          const InventoryAccess &access) {
    auto source = item(handle.id);
    auto target = container(backpack);
    if ((source && !std::holds_alternative<GroundLocation>(source->location)) ||
        (target && target->spec.kind != ContainerKind::Backpack)) {
        InventoryResult result;
        result.error = InventoryError::InvalidRequest;
        return result;
    }
    return transfer({handle, backpack}, access);
}
} // namespace d2x
