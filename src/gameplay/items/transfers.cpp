#include "inventory.hpp"
#include <algorithm>

namespace d2x {
namespace {
InventoryResult failure(InventoryError error) {
    InventoryResult result;
    result.error = error;
    return result;
}
InventoryResult prepared(EntityId item, unsigned quantity) {
    InventoryResult result;
    result.item = item;
    result.transferred = quantity;
    // Allocate notifications before committing. The operations below change at most two instances.
    result.changes.reserve(2);
    return result;
}
} // namespace
InventoryResult InventoryService::createItem(std::string_view code, unsigned quantity,
                                             const ItemDestination &destination, unsigned level) {
    if (level < 1 || level > 99)
        return failure(InventoryError::InvalidRequest);
    auto definition = catalog_.find(code);
    if (!definition)
        return failure(InventoryError::UnknownDefinition);
    if (quantity == 0 || quantity > definition->maxStack)
        return failure(InventoryError::InvalidQuantity);
    ItemLocation location;
    if (auto error = resolve(*definition, destination, location); error != InventoryError::None)
        return failure(error);
    ItemInstance instance;
    instance.definition = definition->code;
    instance.quantity = quantity;
    instance.level = level;
    instance.durability = definition->maxDurability;
    instance.location = location;
    uint64_t nextRandom = state_.creationRandom;
    if (definition->family == ItemFamily::Armor) {
        auto minimum = definition->base.minDefense;
        auto maximum = definition->base.maxDefense;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum || *maximum > 1000000)
            return failure(InventoryError::UnsupportedEquipment);
        nextRandom = uint64_t(uint32_t(nextRandom)) * 0x6ac690c5ULL + (nextRandom >> 32);
        instance.defense = *minimum + uint32_t(nextRandom) % uint32_t(*maximum - *minimum + 1);
    }
    instance.id = ids_.allocate();
    auto result = prepared(instance.id, quantity);
    result.changes.push_back(
        {instance.id, instance.revision, ItemChangeKind::Created, std::nullopt, location, quantity});
    state_.items.emplace(instance.id, std::move(instance));
    state_.creationRandom = nextRandom;
    return result;
}
InventoryResult InventoryService::move(const MoveItem &command, const InventoryAccess &access) {
    if (auto error = preview(command, access); error != InventoryError::None)
        return failure(error);
    auto &source = state_.items.at(command.item.id);
    ItemLocation destination;
    resolve(*catalog_.find(source.definition), command.destination, destination, source.id);
    auto result = prepared(source.id, source.quantity);
    if (source.location == destination) {
        result.transferred = 0;
        return result;
    }
    result.changes.push_back({source.id, source.revision + 1, ItemChangeKind::Moved, source.location,
                              destination, source.quantity});
    source.location = destination;
    ++source.revision;
    return result;
}
InventoryResult InventoryService::swap(const SwapItems &command, const InventoryAccess &access) {
    if (auto error = preview(command, access); error != InventoryError::None)
        return failure(error);
    auto &first = state_.items.at(command.first.id);
    auto &second = state_.items.at(command.second.id);
    auto result = prepared(first.id, first.quantity);
    result.changes.push_back({first.id, first.revision + 1, ItemChangeKind::Moved, first.location,
                              second.location, first.quantity});
    result.changes.push_back({second.id, second.revision + 1, ItemChangeKind::Moved, second.location,
                              first.location, second.quantity});
    std::swap(first.location, second.location);
    ++first.revision;
    ++second.revision;
    return result;
}
InventoryResult InventoryService::split(const SplitStack &command, const InventoryAccess &access) {
    if (auto error = preview(command, access); error != InventoryError::None)
        return failure(error);
    auto &source = state_.items.at(command.source.id);
    ItemLocation destination;
    resolve(*catalog_.find(source.definition), command.destination, destination);
    auto created = source;
    created.id = ids_.allocate();
    created.quantity = command.quantity;
    created.revision = 1;
    created.location = destination;
    auto result = prepared(created.id, command.quantity);
    result.changes.push_back({source.id, source.revision + 1, ItemChangeKind::QuantityChanged,
                              source.location, source.location, source.quantity - command.quantity});
    result.changes.push_back(
        {created.id, created.revision, ItemChangeKind::Created, std::nullopt, destination, created.quantity});
    // std::map insertion may allocate; it must succeed before the original quantity is decremented.
    state_.items.emplace(created.id, std::move(created));
    source.quantity -= command.quantity;
    ++source.revision;
    return result;
}
InventoryResult InventoryService::merge(const MergeStacks &command, const InventoryAccess &access) {
    if (auto error = preview(command, access); error != InventoryError::None)
        return failure(error);
    auto &source = state_.items.at(command.source.id);
    auto &target = state_.items.at(command.target.id);
    const auto &definition = *catalog_.find(source.definition);
    unsigned space = definition.maxStack - target.quantity;
    unsigned quantity = command.quantity == 0 ? std::min(source.quantity, space) : command.quantity;
    auto result = prepared(target.id, quantity);
    unsigned remaining = source.quantity - quantity;
    result.changes.push_back({target.id, target.revision + 1, ItemChangeKind::QuantityChanged,
                              target.location, target.location, target.quantity + quantity});
    result.changes.push_back(
        {source.id, source.revision + 1,
         remaining ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed, source.location,
         remaining ? std::optional<ItemLocation>{source.location} : std::nullopt, remaining});
    target.quantity += quantity;
    ++target.revision;
    if (remaining == 0)
        state_.items.erase(source.id);
    else {
        source.quantity = remaining;
        ++source.revision;
    }
    return result;
}
InventoryResult InventoryService::consume(ItemHandle handle, unsigned quantity,
                                          const InventoryAccess &access) {
    if (auto error = checkHandle(handle); error != InventoryError::None)
        return failure(error);
    auto &source = state_.items.at(handle.id);
    if (quantity == 0 || quantity > source.quantity)
        return failure(InventoryError::InvalidQuantity);
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return failure(error);
    auto result = prepared(source.id, quantity);
    unsigned remaining = source.quantity - quantity;
    result.changes.push_back(
        {source.id, source.revision + 1,
         remaining ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed, source.location,
         remaining ? std::optional<ItemLocation>{source.location} : std::nullopt, remaining});
    if (remaining == 0)
        state_.items.erase(source.id);
    else {
        source.quantity = remaining;
        ++source.revision;
    }
    return result;
}
} // namespace d2x
