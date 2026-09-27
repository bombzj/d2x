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
bool samePotionFamily(const ItemDefinition &a, const ItemDefinition &b) {
    if (a.code == b.code)
        return true;
    return (a.base.type == "hpot" || a.base.type == "mpot" || a.base.type == "rpot") &&
           a.base.type == b.base.type;
}
} // namespace
std::optional<Cell> InventoryService::beltSpace(EntityId belt, std::string_view code,
                                                bool automaticPickup) const {
    auto c = container(belt);
    auto def = catalog_.find(code);
    if (!c || c->spec.kind != ContainerKind::Belt || !def || !def->beltAllowed || def->width != 1 ||
        def->height != 1)
        return std::nullopt;
    // Fill matching columns upwards before claiming a completely empty column.
    for (int pass = 0; pass < 2; ++pass)
        for (int x = 0; x < c->spec.columns; ++x) {
            const ItemInstance *anchor = nullptr;
            for (int y = 0; y < c->spec.rows && !anchor; ++y)
                anchor = item(itemAt(belt, {x, y}));
            if (pass == 0 ? !anchor || !samePotionFamily(*def, *catalog_.find(anchor->definition))
                          : anchor || (automaticPickup && !def->autoBelt))
                continue;
            for (int y = 0; y < c->spec.rows; ++y)
                if (!itemAt(belt, {x, y}))
                    return Cell{x, y};
        }
    return std::nullopt;
}
InventoryResult InventoryService::planBelt(const EquipBelt &command, const PlayerContainers &containers,
                                           const InventoryAccess &access, const EquipmentActor &actor,
                                           InventoryState *replacement) const {
    if (auto error = checkHandle(command.item); error != InventoryError::None)
        return failure(error);
    if (!access.alive || !access.actor)
        return failure(InventoryError::AccessDenied);
    const std::pair<EntityId, ContainerKind> required[] = {
        {containers.backpack, ContainerKind::Backpack},
        {containers.belt, ContainerKind::Belt},
        {containers.beltEquipment, ContainerKind::BeltEquipment}};
    for (auto [id, kind] : required) {
        auto c = container(id);
        if (!c || c->spec.owner != access.actor || c->spec.kind != kind)
            return failure(InventoryError::AccessDenied);
    }
    const auto &source = *item(command.item.id);
    auto location = std::get_if<ContainerLocation>(&source.location);
    if (!location ||
        (location->container != containers.backpack && location->container != containers.beltEquipment &&
         location->container != containers.cursor))
        return failure(InventoryError::AccessDenied);
    auto definition = catalog_.find(source.definition);
    if (!definition->beltRows)
        return failure(InventoryError::RestrictedItem);
    bool removing = location->container == containers.beltEquipment;
    if (!removing && command.destination)
        return failure(InventoryError::InvalidRequest);
    if (!removing)
        if (auto error = equipmentRequirements(command.item, actor); error != InventoryError::None)
            return failure(error);
    int rows = removing ? 1 : definition->beltRows;
    // Plan against a private snapshot. No IDs are allocated, and failed previews
    // or capacity changes never mutate live items, revisions or container sizes.
    InventoryService draft(ids_, catalog_, stashDimensions_, cubeDimensions_);
    draft.itemProperties_ = itemProperties_;
    draft.groundPlacement_ = groundPlacement_;
    draft.state_ = state_;
    InventoryResult result;
    result.item = source.id;
    result.transferred = 1;
    result.changes.reserve(20);
    auto relocate = [&](EntityId id, const ItemLocation &destination) {
        auto &instance = draft.state_.items.at(id);
        if (instance.revision == std::numeric_limits<uint64_t>::max())
            return false;
        result.changes.push_back({id, instance.revision + 1, ItemChangeKind::Moved, instance.location,
                                  destination, instance.quantity});
        instance.location = destination;
        ++instance.revision;
        return true;
    };
    if (removing) {
        ItemDestination target = command.destination.value_or(AutoPlace{containers.backpack});
        if (auto error = draft.checkDestinationAccess(target, access); error != InventoryError::None)
            return failure(error);
        ItemLocation destination;
        if (auto error = draft.resolve(*definition, target, destination, source.id); error != InventoryError::None)
            return failure(error);
        if (!relocate(source.id, destination))
            return failure(InventoryError::RevisionExhausted);
    } else {
        auto previous = itemAt(containers.beltEquipment, {0, 0});
        if (!relocate(source.id, ContainerLocation{containers.beltEquipment, {0, 0}}))
            return failure(InventoryError::RevisionExhausted);
        if (previous) {
            const auto &old = *draft.item(previous);
            auto target = ContainerLocation{location->container, location->cell};
            if (draft.checkPlacement(*catalog_.find(old.definition), target) != InventoryError::None)
                return failure(InventoryError::NoSpace);
            if (!relocate(previous, target))
                return failure(InventoryError::RevisionExhausted);
        }
    }
    for (auto id : draft.contents(containers.belt)) {
        const auto &instance = *draft.item(id);
        auto cell = std::get<ContainerLocation>(instance.location).cell;
        if (cell.y < rows)
            continue;
        auto space = draft.findSpace(containers.backpack, instance.definition);
        if (!space)
            return failure(InventoryError::NoSpace);
        if (!relocate(id, ContainerLocation{containers.backpack, *space}))
            return failure(InventoryError::RevisionExhausted);
    }
    draft.state_.containers.at(containers.belt).spec.rows = rows;
    if (replacement)
        *replacement = std::move(draft.state_);
    return result;
}
InventoryError InventoryService::preview(const EquipBelt &command, const PlayerContainers &containers,
                                         const InventoryAccess &access, const EquipmentActor &actor) const {
    return planBelt(command, containers, access, actor, nullptr).error;
}
InventoryResult InventoryService::equipBelt(const EquipBelt &command, const PlayerContainers &containers,
                                            const InventoryAccess &access, const EquipmentActor &actor) {
    InventoryState next;
    auto result = planBelt(command, containers, access, actor, &next);
    if (result)
        state_ = std::move(next);
    return result;
}
InventoryError InventoryService::previewDrink(ItemHandle handle, const InventoryAccess &access) const {
    if (auto error = checkHandle(handle); error != InventoryError::None)
        return error;
    const auto &source = *item(handle.id);
    auto location = std::get_if<ContainerLocation>(&source.location);
    if (!location)
        return InventoryError::AccessDenied;
    auto c = container(location->container);
    if (!c || (c->spec.kind != ContainerKind::Belt && c->spec.kind != ContainerKind::Backpack))
        return InventoryError::AccessDenied;
    if (auto error = checkAccess(source.location, access); error != InventoryError::None)
        return error;
    if (c->spec.kind == ContainerKind::Belt)
        for (auto id : contents(c->id)) {
            auto other = item(id);
            if (std::get<ContainerLocation>(other->location).cell.x == location->cell.x &&
                other->revision == std::numeric_limits<uint64_t>::max())
                return InventoryError::RevisionExhausted;
        }
    return InventoryError::None;
}
InventoryResult InventoryService::drink(ItemHandle handle, const InventoryAccess &access) {
    if (auto error = previewDrink(handle, access); error != InventoryError::None)
        return failure(error);
    const auto location = std::get<ContainerLocation>(item(handle.id)->location);
    const auto c = container(location.container);
    InventoryResult result;
    result.changes.reserve(5);
    auto consumed = consume(handle, 1, access);
    if (!consumed)
        return consumed;
    result.item = handle.id;
    result.transferred = 1;
    result.changes.insert(result.changes.end(), consumed.changes.begin(), consumed.changes.end());
    if (c->spec.kind == ContainerKind::Belt && !item(handle.id)) {
        int ready = 0;
        for (int y = 0; y < c->spec.rows; ++y) {
            auto id = itemAt(c->id, {location.cell.x, y});
            if (!id)
                continue;
            auto &instance = state_.items.at(id);
            if (ready != y) {
                ItemLocation destination = ContainerLocation{c->id, {location.cell.x, ready}};
                result.changes.push_back({id, instance.revision + 1, ItemChangeKind::Moved, instance.location,
                                          destination, instance.quantity});
                instance.location = destination;
                ++instance.revision;
            }
            ++ready;
        }
    }
    return result;
}
} // namespace d2x
