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
         destination->spec.kind != ContainerKind::Stash && destination->spec.kind != ContainerKind::Chest &&
         destination->spec.kind != ContainerKind::Cube))
        return failure(InventoryError::UnknownContainer);
    if (auto error = checkDestinationAccess(AutoPlace{backpack}, access); error != InventoryError::None)
        return failure(error);
    if (auto error = checkCarryLimit(source, ContainerLocation{backpack, {}}, state_);
        error != InventoryError::None)
        return failure(error);
    const auto &definition = *catalog_.find(source.definition);
    if (definition.equipment.isType("gold"))
        return failure(InventoryError::RestrictedItem);
    unsigned remaining = source.quantity;
    std::vector<std::pair<EntityId, unsigned>> merges;
    if (remaining && definition.maxStack > 1)
        for (auto id : contents(backpack)) {
            const auto &target = state_.items.at(id);
            if (!stackablesEqual(source, target) || target.quantity >= maximumStack(target))
                continue;
            if (auto error = checkHandle(target.handle()); error != InventoryError::None)
                return failure(error);
            unsigned amount = std::min(remaining, maximumStack(target) - target.quantity);
            merges.emplace_back(id, amount);
            remaining -= amount;
            if (!remaining)
                break;
        }
    std::optional<Cell> slot;
    const bool emptyWeapon = source.quantity == 0 && definition.equipment.throwable &&
                             definition.equipment.repairable;
    if (remaining || emptyWeapon) {
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
    if (remaining || emptyWeapon)
        after = ContainerLocation{backpack, *slot};
    result.changes.push_back({source.id, source.revision + 1,
                              (remaining || emptyWeapon) ? ItemChangeKind::Moved : ItemChangeKind::Removed, source.location,
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
bool InventoryService::stackablesEqual(const ItemInstance &first, const ItemInstance &second) const {
    // D2Common ITEMS_AreStackablesEqual: same base, quality/file index, ethereal state,
    // six physical damage stats and no sockets. Magical qualities never merge.
    if (first.definition != second.definition || first.quality != second.quality ||
        first.gradeRow != second.gradeRow || first.specialRow != second.specialRow ||
        (first.quality != ItemQuality::Normal && first.quality != ItemQuality::Superior &&
         first.quality != ItemQuality::Inferior) ||
        ((first.nativeFlags ^ second.nativeFlags) & 0x00400000u) ||
        first.grantedSkill != second.grantedSkill || first.sockets || second.sockets ||
        catalog_.find(first.definition)->maxStack <= 1)
        return false;
    for (auto stat : {"mindamage", "maxdamage", "secondary_mindamage", "secondary_maxdamage",
                      "item_throw_mindamage", "item_throw_maxdamage",
                      "item_mindamage_percent", "item_maxdamage_percent"})
        if (propertyValue(first, stat) != propertyValue(second, stat)) return false;
    return !propertyValue(first, "item_numsockets") && !propertyValue(second, "item_numsockets");
}
InventoryResult InventoryService::collect(ItemHandle handle, const PlayerContainers &containers,
                                          const InventoryAccess &access) {
    auto failure = [](InventoryError error) {
        InventoryResult result; result.error = error; return result;
    };
    if (auto error = checkHandle(handle); error != InventoryError::None) return failure(error);
    const auto &source = *item(handle.id);
    if (!std::holds_alternative<GroundLocation>(source.location))
        return failure(InventoryError::InvalidRequest);
    if (auto error = checkAccess(source.location, access); error != InventoryError::None) return failure(error);
    for (auto [id, kind] : {std::pair{containers.backpack, ContainerKind::Backpack},
                           std::pair{containers.belt, ContainerKind::Belt},
                           std::pair{containers.equipment, ContainerKind::Equipment}}) {
        const auto *c = container(id);
        if (!c || c->spec.kind != kind || c->spec.owner != access.actor)
            return failure(InventoryError::AccessDenied);
    }
    if (auto error = checkCarryLimit(source, ContainerLocation{containers.backpack, {}}, state_);
        error != InventoryError::None) return failure(error);
    const auto &definition = *catalog_.find(source.definition);
    if (definition.equipment.isType("gold")) return failure(InventoryError::RestrictedItem);
    InventoryService draft(ids_, catalog_, stashDimensions_, cubeDimensions_);
    draft.state_ = state_;
    draft.itemProperties_ = itemProperties_;
    draft.groundPlacement_ = groundPlacement_;
    draft.singleCarryUniques_ = singleCarryUniques_;
    InventoryResult result;
    result.item = handle.id;
    auto append = [&](InventoryResult change) {
        result.transferred += change.transferred;
        result.changes.insert(result.changes.end(), change.changes.begin(), change.changes.end());
    };
    // Scrolls and books use Books pairing/charges, not ordinary stack quantity.
    for (auto id : draft.contents(containers.backpack)) {
        const auto *left = draft.item(handle.id);
        const auto *book = draft.item(id);
        if (!left) break;
        const auto *type = catalog_.find(book->definition);
        if (!type->bookCapacity || book->charges >= type->bookCapacity ||
            (type->bookScroll != left->definition && book->definition != left->definition)) continue;
        auto loaded = draft.loadBook({left->handle(), book->handle()}, access);
        if (!loaded) return failure(loaded.error);
        append(std::move(loaded));
        // D2Game sub_6FC43BF0 fills one matching tome per pickup. Its excess stays on the ground.
        state_ = std::move(draft.state_);
        return result;
    }
    if (definition.autoStack) {
        // Equipped stacks have priority; then eligible carried stacks (AutoStack).
        for (auto containerId : {containers.equipment, containers.backpack}) {
            for (auto id : draft.contents(containerId)) {
                auto *left = draft.item(handle.id);
                auto &target = draft.state_.items.at(id);
                if (!left) break;
                if (!draft.stackablesEqual(*left, target) || target.quantity >= draft.maximumStack(target)) continue;
                if (auto error = draft.checkHandle(target.handle()); error != InventoryError::None)
                    return failure(error);
                const unsigned amount = std::min(left->quantity, draft.maximumStack(target) - target.quantity);
                if (!amount) continue;
                const unsigned remaining = left->quantity - amount;
                if (!remaining && definition.maxDurability)
                    target.durability = std::min(target.durability, left->durability);
                target.quantity += amount;
                ++target.revision;
                result.transferred += amount;
                result.changes.push_back({id, target.revision, ItemChangeKind::QuantityChanged,
                    target.location, target.location, target.quantity});
                result.changes.push_back({left->id, left->revision + 1,
                    remaining ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed,
                    left->location, remaining ? std::optional<ItemLocation>{left->location} : std::nullopt,
                    remaining});
                if (!remaining) draft.state_.items.erase(handle.id);
                else {
                    auto &remainder = draft.state_.items.at(handle.id);
                    remainder.quantity = remaining;
                    ++remainder.revision;
                }
            }
        }
    }
    if (const auto *left = draft.item(handle.id)) {
        auto belt = draft.beltSpace(containers.belt, left->definition, true);
        ItemDestination destination = belt ? ItemDestination{ContainerLocation{containers.belt, *belt}}
                                           : ItemDestination{AutoPlace{containers.backpack}};
        auto placed = draft.move({left->handle(), destination}, access);
        if (placed) append(std::move(placed));
        else if (placed.error != InventoryError::NoSpace || result.changes.empty())
            return failure(placed.error);
    }
    state_ = std::move(draft.state_);
    return result;
}
} // namespace d2x
