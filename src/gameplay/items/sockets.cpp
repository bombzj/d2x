#include "inventory.hpp"
#include <limits>

namespace d2x {
InventoryError InventoryService::preview(const SocketItem &command, const InventoryAccess &access) const {
    if (command.filler.id == command.host.id) return InventoryError::InvalidRequest;
    if (auto error = checkHandle(command.filler); error != InventoryError::None) return error;
    if (auto error = checkHandle(command.host); error != InventoryError::None) return error;
    const auto &filler = state_.items.at(command.filler.id);
    const auto &host = state_.items.at(command.host.id);
    const auto *base = catalog_.find(host.definition);
    const auto *fill = catalog_.find(filler.definition);
    if (!host.identified || !filler.identified) return InventoryError::Unidentified;
    if (!fill->equipment.isType("sock") || filler.quantity != 1 || !filler.socketedItems.empty() ||
        !base->base.sockets.value_or(0) || base->maxStack > 1 || !host.sockets ||
        base->gemApplyType < 0 || base->gemApplyType > 2 || host.quantity != 1 || !prepareSockets_)
        return InventoryError::RestrictedItem;
    if (host.socketedItems.size() >= host.sockets) return InventoryError::NoSpace;
    if (filler.revision == UINT64_MAX || host.revision == UINT64_MAX) return InventoryError::RevisionExhausted;
    // UI dragging keeps the filler in its original container until commit.
    if (auto error = checkAccess(filler.location, access); error != InventoryError::None) return error;
    const auto *position = std::get_if<ContainerLocation>(&host.location);
    if (!position) return InventoryError::AccessDenied;
    const auto *storage = container(position->container);
    if (!storage || storage->spec.owner != access.actor || storage->spec.kind == ContainerKind::Corpse ||
        storage->spec.kind == ContainerKind::Cursor || storage->spec.kind == ContainerKind::Belt)
        return InventoryError::AccessDenied;
    if (storage->spec.kind == ContainerKind::Equipment) return InventoryError::None;
    return checkAccess(host.location, access);
}
InventoryResult InventoryService::socket(const SocketItem &command, const InventoryAccess &access) {
    InventoryResult result;
    if (auto error = preview(command, access); error != InventoryError::None) {
        result.error = error; return result;
    }
    // Prepare values, RNG and events before replacing either live instance.
    auto host = state_.items.at(command.host.id);
    auto child = state_.items.at(command.filler.id);
    const auto previous = child.location;
    child.location = SocketLocation{host.id, unsigned(host.socketedItems.size())};
    ++child.revision;
    host.socketedItems.push_back(std::move(child));
    auto random = state_.creationRandom;
    prepareSockets_(host, random);
    ++host.revision;
    result.item = host.id; result.transferred = 1;
    result.changes.push_back({command.filler.id, command.filler.revision + 1, ItemChangeKind::Removed,
        previous, std::nullopt, 0});
    result.changes.push_back({host.id, host.revision, ItemChangeKind::PropertiesChanged,
        host.location, host.location, host.quantity});
    state_.items.at(host.id) = std::move(host);
    state_.items.erase(command.filler.id);
    state_.creationRandom = random;
    return result;
}
} // namespace d2x
