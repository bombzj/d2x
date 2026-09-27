#include "inventory.hpp"
#include <algorithm>
#include <limits>

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
                                             const ItemDestination &destination, unsigned level,
                                             const ItemGeneration &generation, std::optional<Vec> groundOrigin) {
    if (level < 1 || level > 99)
        return failure(InventoryError::InvalidRequest);
    auto definition = catalog_.find(code);
    if (!definition)
        return failure(InventoryError::UnknownDefinition);
    if (quantity == 0)
        return failure(InventoryError::InvalidQuantity);
    if (generation.specialRow < -1 || generation.gradeRow < -1 ||
        generation.rarePrefixRow < -1 || generation.rareSuffixRow < -1 ||
        generation.requiredLevel < 0 || generation.requiredLevel > 99 ||
        (generation.quality == ItemQuality::Normal &&
         (generation.specialRow != -1 || generation.gradeRow != -1 ||
          generation.rarePrefixRow != -1 || generation.rareSuffixRow != -1 ||
          !generation.propertyRolls.empty() || !generation.affixes.empty())) ||
        ((generation.quality == ItemQuality::Set || generation.quality == ItemQuality::Unique) &&
         (generation.specialRow < 0 || generation.gradeRow != -1 ||
          generation.rarePrefixRow != -1 || generation.rareSuffixRow != -1 ||
          !generation.affixes.empty())) ||
        ((generation.quality == ItemQuality::Magic || generation.quality == ItemQuality::Rare) &&
         (generation.specialRow != -1 || generation.gradeRow != -1 || generation.affixes.empty())) ||
        ((generation.quality == ItemQuality::Superior || generation.quality == ItemQuality::Inferior) &&
         (generation.gradeRow < 0 || generation.specialRow != -1 ||
          generation.rarePrefixRow != -1 || generation.rareSuffixRow != -1 ||
          !generation.affixes.empty())) ||
        (generation.quality == ItemQuality::Rare &&
         (generation.rarePrefixRow < 0 || generation.rareSuffixRow < 0)) ||
        (generation.quality == ItemQuality::Magic &&
         (generation.rarePrefixRow != -1 || generation.rareSuffixRow != -1)) ||
        int(generation.quality) < 0 || int(generation.quality) > int(ItemQuality::Inferior))
        return failure(InventoryError::InvalidRequest);
    ItemLocation location;
    if (auto error = resolve(*definition, destination, location, {}, groundOrigin); error != InventoryError::None)
        return failure(error);
    ItemInstance instance;
    instance.definition = definition->code;
    instance.quantity = quantity;
    instance.charges = definition->bookInitialCharges;
    instance.level = level;
    instance.quality = generation.quality;
    instance.identified = generation.quality != ItemQuality::Magic &&
                          generation.quality != ItemQuality::Rare &&
                          generation.quality != ItemQuality::Set &&
                          generation.quality != ItemQuality::Unique;
    instance.specialRow = generation.specialRow;
    instance.requiredLevel = generation.requiredLevel;
    instance.gradeRow = generation.gradeRow;
    instance.rarePrefixRow = generation.rarePrefixRow;
    instance.rareSuffixRow = generation.rareSuffixRow;
    instance.propertyRolls = generation.propertyRolls;
    instance.affixes = generation.affixes;
    instance.durability = definition->maxDurability;
    instance.location = location;
    instance.durability = maximumDurability(instance);
    if (auto error = checkCarryLimit(instance, location, state_); error != InventoryError::None)
        return failure(error);
    if (quantity > maximumStack(instance)) return failure(InventoryError::InvalidQuantity);
    uint64_t nextRandom = state_.creationRandom;
    if (!definition->inventoryIcons.empty()) {
        nextRandom = uint64_t(uint32_t(nextRandom)) * 0x6ac690c5ULL + (nextRandom >> 32);
        instance.nativeHasGraphic = true;
        instance.nativeGraphic = uint32_t(nextRandom) % uint32_t(definition->inventoryIcons.size());
    }
    if (definition->family == ItemFamily::Armor) {
        auto minimum = definition->base.minDefense;
        auto maximum = definition->base.maxDefense;
        if (!minimum || !maximum || *minimum < 0 || *maximum < *minimum || *maximum > 1000000)
            return failure(InventoryError::UnsupportedEquipment);
        nextRandom = uint64_t(uint32_t(nextRandom)) * 0x6ac690c5ULL + (nextRandom >> 32);
        instance.defense = *minimum + uint32_t(nextRandom) % uint32_t(*maximum - *minimum + 1);
        if (instance.quality == ItemQuality::Inferior) instance.defense = std::max(1, instance.defense * 75 / 100);
        if (propertyValue(instance, "item_armor_percent")) instance.defense = *maximum + 1;
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
    unsigned space = maximumStack(target) - target.quantity;
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
InventoryResult InventoryService::consumeEquipped(EntityId id, const PlayerContainers &containers) {
    if (id != equipped(containers, EquipmentSlot::RightHand) &&
        id != equipped(containers, EquipmentSlot::LeftHand) &&
        id != equipped(containers, EquipmentSlot::AlternateRightHand) &&
        id != equipped(containers, EquipmentSlot::AlternateLeftHand))
        return failure(InventoryError::AccessDenied);
    const auto *source = item(id);
    if (!source || source->quantity == 0 || source->revision == std::numeric_limits<uint64_t>::max())
        return failure(InventoryError::InvalidRequest);
    auto result = prepared(id, 1);
    const unsigned remaining = source->quantity - 1;
    const bool retain = remaining || retainsEmptyStack(*source);
    result.changes.push_back({id, source->revision + 1,
        retain ? ItemChangeKind::QuantityChanged : ItemChangeKind::Removed,
        source->location,
        retain ? std::optional<ItemLocation>{source->location} : std::nullopt, remaining});
    if (retain) {
        auto &instance = state_.items.at(id);
        instance.quantity = remaining;
        instance.durability = maximumDurability(instance);
        ++instance.revision;
    } else state_.items.erase(id);
    return result;
}
} // namespace d2x
