#include "inventory.hpp"
#include <algorithm>

namespace d2x {
InventoryError InventoryService::preview(const MoveItem &command, const InventoryAccess &access) const {
    if (auto error = checkHandle(command.item); error != InventoryError::None)
        return error;
    const auto &source = state_.items.at(command.item.id);
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return error;
    if (auto error = checkDestinationAccess(command.destination, access); error != InventoryError::None)
        return error;
    ItemLocation destination;
    if (auto error = resolve(*catalog_.find(source.definition), command.destination, destination, source.id);
        error != InventoryError::None)
        return error;
    return InventoryError::None;
}
InventoryError InventoryService::preview(const SwapItems &command, const InventoryAccess &access) const {
    if (command.first.id == command.second.id)
        return InventoryError::InvalidRequest;
    if (auto error = checkHandle(command.first); error != InventoryError::None)
        return error;
    if (auto error = checkHandle(command.second); error != InventoryError::None)
        return error;
    const auto &first = state_.items.at(command.first.id);
    const auto &second = state_.items.at(command.second.id);
    if (auto error = checkAccess(first.location, access); error != InventoryError::None)
        return error;
    if (auto error = checkAccess(second.location, access); error != InventoryError::None)
        return error;
    const auto &a = *catalog_.find(first.definition);
    const auto &b = *catalog_.find(second.definition);
    if (auto error = checkPlacement(a, second.location, first.id, second.id); error != InventoryError::None)
        return error;
    if (auto error = checkPlacement(b, first.location, first.id, second.id); error != InventoryError::None)
        return error;
    // Ignoring both old footprints is insufficient: the proposed new footprints must not overlap each other.
    if (overlaps(a, second.location, b, first.location))
        return InventoryError::Occupied;
    return InventoryError::None;
}
InventoryError InventoryService::preview(const SplitStack &command, const InventoryAccess &access) const {
    if (auto error = checkHandle(command.source); error != InventoryError::None)
        return error;
    const auto &source = state_.items.at(command.source.id);
    const auto &definition = *catalog_.find(source.definition);
    if (definition.maxStack <= 1 || source.quality != ItemQuality::Normal)
        return InventoryError::NotStackable;
    if (command.quantity == 0 || command.quantity >= source.quantity)
        return InventoryError::InvalidQuantity;
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return error;
    if (auto error = checkDestinationAccess(command.destination, access); error != InventoryError::None)
        return error;
    ItemLocation destination;
    if (auto error = resolve(definition, command.destination, destination); error != InventoryError::None)
        return error;
    return InventoryError::None;
}
InventoryError InventoryService::preview(const MergeStacks &command, const InventoryAccess &access) const {
    if (command.source.id == command.target.id)
        return InventoryError::InvalidRequest;
    if (auto error = checkHandle(command.source); error != InventoryError::None)
        return error;
    if (auto error = checkHandle(command.target); error != InventoryError::None)
        return error;
    const auto &source = state_.items.at(command.source.id);
    const auto &target = state_.items.at(command.target.id);
    const auto &definition = *catalog_.find(source.definition);
    if (definition.maxStack <= 1)
        return InventoryError::NotStackable;
    if (source.definition != target.definition || source.quality != ItemQuality::Normal ||
        target.quality != ItemQuality::Normal)
        return InventoryError::IncompatibleStack;
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return error;
    if (auto error = checkAccess(target.location, access); error != InventoryError::None)
        return error;
    unsigned space = maximumStack(target) - target.quantity;
    unsigned quantity = command.quantity == 0 ? std::min(source.quantity, space) : command.quantity;
    if (!quantity) return InventoryError::InvalidQuantity;
    if (command.quantity > source.quantity)
        return InventoryError::InvalidQuantity;
    if (space == 0 || quantity > space)
        return InventoryError::StackFull;
    return InventoryError::None;
}
} // namespace d2x
