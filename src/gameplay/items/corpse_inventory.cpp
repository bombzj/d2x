#include "inventory.hpp"
#include <limits>

namespace d2x {
namespace {
InventoryResult rejected(InventoryError error) { InventoryResult r; r.error = error; return r; }
}
InventoryResult InventoryService::detachDeathEquipment(const PlayerContainers &containers, EntityId corpse,
                                                       const GroundLocation &ground) {
    InventoryService draft(ids_, catalog_, stashDimensions_, cubeDimensions_);
    draft.state_ = state_; draft.itemProperties_ = itemProperties_;
    draft.groundPlacement_ = groundPlacement_;
    InventoryResult result;
    auto relocate = [&](EntityId id, const ItemLocation &destination) {
        auto &item = draft.state_.items.at(id);
        if (item.revision == std::numeric_limits<uint64_t>::max()) return false;
        result.changes.push_back({id, item.revision + 1, ItemChangeKind::Moved, item.location,
                                  destination, item.quantity});
        item.location = destination; ++item.revision;
        return true;
    };
    for (int slot = 0; slot <= int(EquipmentSlot::Count); ++slot) {
        const auto id = slot == int(EquipmentSlot::Count) ? draft.itemAt(containers.cursor, {}) :
            draft.equipped(containers, EquipmentSlot(slot));
        if (!id) continue;
        ItemLocation destination = ContainerLocation{corpse, {slot, 0}};
        if (!corpse) {
            const auto &item = *draft.item(id);
            const auto error = draft.resolve(*catalog_.find(item.definition), ground, destination, id, ground.position);
            if (error != InventoryError::None) return rejected(error);
        }
        if (!relocate(id, destination)) return rejected(InventoryError::RevisionExhausted);
    }
    // Removing a belt retains its first four potion slots; excess potions move
    // into the backpack, otherwise onto the ground (ItemMode.sub_6FC45930).
    for (auto id : draft.contents(containers.belt)) {
        const auto &item = *draft.item(id);
        if (std::get<ContainerLocation>(item.location).cell.y == 0) continue;
        ItemLocation destination;
        if (auto cell = draft.findSpace(containers.backpack, item.definition))
            destination = ContainerLocation{containers.backpack, *cell};
        else {
            const auto error = draft.resolve(*catalog_.find(item.definition), ground, destination, id, ground.position);
            if (error != InventoryError::None) return rejected(error);
        }
        if (!relocate(id, destination)) return rejected(InventoryError::RevisionExhausted);
    }
    draft.state_.containers.at(containers.belt).spec.rows = 1;
    state_ = std::move(draft.state_);
    return result;
}
InventoryResult InventoryService::recoverCorpseItem(ItemHandle handle, const PlayerContainers &containers,
                                                   const InventoryAccess &access, const EquipmentActor &actor,
                                                   std::optional<EquipmentSlot> slot) {
    if (auto error = checkHandle(handle); error != InventoryError::None) return rejected(error);
    const auto &source = *item(handle.id);
    const auto *location = std::get_if<ContainerLocation>(&source.location);
    const auto *storage = location ? container(location->container) : nullptr;
    if (!access.alive || !storage || storage->spec.kind != ContainerKind::Corpse ||
        storage->spec.owner != access.actor || itemAt(containers.cursor, {}))
        return rejected(InventoryError::AccessDenied);
    if (source.revision == std::numeric_limits<uint64_t>::max()) return rejected(InventoryError::RevisionExhausted);
    InventoryService draft(ids_, catalog_, stashDimensions_, cubeDimensions_);
    draft.state_ = state_; draft.itemProperties_ = itemProperties_;
    draft.groundPlacement_ = groundPlacement_; draft.singleCarryUniques_ = singleCarryUniques_;
    InventoryResult result;
    if (slot) {
        if (equipped(containers, *slot)) return rejected(InventoryError::Occupied);
        const auto destination = *slot == EquipmentSlot::Belt
            ? ContainerLocation{containers.beltEquipment, {}}
            : ContainerLocation{containers.equipment, {int(*slot), 0}};
        if (auto error = checkCarryLimit(source, destination, state_); error != InventoryError::None)
            return rejected(error);
        // A private cursor bridge lets ordinary equipment rules validate hands,
        // class and requirements without publishing any transient cursor state.
        draft.state_.items.at(handle.id).location = ContainerLocation{containers.cursor, {}};
        InventoryState next;
        result = *slot == EquipmentSlot::Belt
            ? draft.planBelt(EquipBelt{handle}, containers, access, actor, &next)
            : draft.planEquipment(EquipItem{handle, *slot}, containers, access, actor, &next);
        if (!result) return result;
        for (const auto &change : result.changes)
            if (change.item != handle.id) return rejected(InventoryError::Occupied);
        for (auto &change : result.changes) change.before = source.location;
        draft.state_ = std::move(next);
    } else {
        // Reuse normal collection for potion auto-belt, tome charges and stack
        // merges. The private source bridge is never a live world drop.
        draft.state_.items.at(handle.id).location = GroundLocation{access.region, access.position};
        result = draft.collect(handle, containers, access);
        if (!result) return result;
        if (auto remaining = draft.state_.items.find(handle.id); remaining != draft.state_.items.end() &&
            std::holds_alternative<GroundLocation>(remaining->second.location))
            remaining->second.location = source.location;
        for (auto &change : result.changes)
            if (change.item == handle.id) {
                if (change.before && std::holds_alternative<GroundLocation>(*change.before)) change.before = source.location;
                if (change.after && std::holds_alternative<GroundLocation>(*change.after)) change.after = source.location;
            }
    }
    state_ = std::move(draft.state_);
    return result;
}
} // namespace d2x
