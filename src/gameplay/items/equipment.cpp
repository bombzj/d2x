#include "inventory.hpp"
#include "equipment_stats.hpp"
#include <algorithm>
#include <limits>

namespace d2x {
namespace {
InventoryResult reject(InventoryError error) {
    InventoryResult result;
    result.error = error;
    return result;
}
bool occupiesBothHands(const ItemDefinition &definition, const EquipmentActor &actor) {
    return definition.equipment.twoHanded &&
           !(actor.characterClass == "bar" && definition.equipment.oneOrTwoHanded);
}
bool compatibleHands(const ItemDefinition &first, const ItemDefinition &second, const EquipmentActor &actor) {
    const auto &left = first.equipment;
    const auto &right = second.equipment;
    if (!left.shoots.empty() || !right.shoots.empty())
        return (!left.shoots.empty() && right.isType(left.shoots) && !right.quiver.empty() &&
                left.isType(right.quiver)) ||
               (!right.shoots.empty() && left.isType(right.shoots) && !left.quiver.empty() &&
                right.isType(left.quiver));
    if (!left.quiver.empty() || !right.quiver.empty() || occupiesBothHands(first, actor) ||
        occupiesBothHands(second, actor))
        return false;
    if (left.isType("shld") || right.isType("shld"))
        return left.isType("shld") ? right.isType("weap") : left.isType("weap");
    if (!left.isType("weap") || !right.isType("weap"))
        return false;
    return actor.characterClass == "bar" ||
           (actor.characterClass == "ass" && left.isType("h2h") && right.isType("h2h"));
}
} // namespace
EntityId InventoryService::equipped(const PlayerContainers &containers, EquipmentSlot slot) const {
    if (slot == EquipmentSlot::Belt)
        return itemAt(containers.beltEquipment, {});
    return itemAt(containers.equipment, {int(slot), 0});
}
InventoryError InventoryService::equipmentRequirements(ItemHandle handle, const EquipmentActor &actor) const {
    if (auto error = checkHandle(handle); error != InventoryError::None)
        return error;
    const auto &source = *item(handle.id);
    const auto &definition = *catalog_.find(source.definition);
    if (!definition.equipment.known || source.quality != ItemQuality::Normal ||
        definition.equipment.types.empty() || definition.equipment.isType("tpot"))
        return InventoryError::UnsupportedEquipment;
    if (!definition.equipment.requiredClass.empty() &&
        definition.equipment.requiredClass != actor.characterClass)
        return InventoryError::WrongClass;
    if (actor.strength < definition.base.requiredStrength.value_or(0) ||
        actor.dexterity < definition.base.requiredDexterity.value_or(0) ||
        actor.level < definition.base.requiredLevel.value_or(0))
        return InventoryError::RequirementsNotMet;
    return InventoryError::None;
}
InventoryResult InventoryService::planEquipment(const EquipItem &command, const PlayerContainers &containers,
                                                const InventoryAccess &access, const EquipmentActor &actor,
                                                InventoryState *replacement) const {
    if (command.slot && command.destination)
        return reject(InventoryError::InvalidRequest);
    if (auto error = checkHandle(command.item); error != InventoryError::None)
        return reject(error);
    if (!access.alive || !access.actor)
        return reject(InventoryError::AccessDenied);
    for (auto [id, kind] : {std::pair{containers.backpack, ContainerKind::Backpack},
                            std::pair{containers.equipment, ContainerKind::Equipment}}) {
        auto storage = container(id);
        if (!storage || storage->spec.owner != access.actor || storage->spec.kind != kind)
            return reject(InventoryError::AccessDenied);
    }
    const auto &source = *item(command.item.id);
    auto location = std::get_if<ContainerLocation>(&source.location);
    if (!location ||
        (location->container != containers.backpack && location->container != containers.equipment))
        return reject(InventoryError::AccessDenied);
    const auto &definition = *catalog_.find(source.definition);
    if (command.slot) {
        if (*command.slot == EquipmentSlot::Belt || !definition.equipment.fits(*command.slot))
            return reject(InventoryError::RestrictedItem);
        if (auto error = equipmentRequirements(command.item, actor); error != InventoryError::None)
            return reject(error);
    } else if (location->container != containers.equipment)
        return reject(InventoryError::InvalidRequest);
    InventoryService draft(ids_, catalog_);
    draft.state_ = state_;
    InventoryResult result;
    result.item = source.id;
    result.transferred = source.quantity;
    auto relocate = [&](EntityId id, const ItemLocation &destination) {
        auto &instance = draft.state_.items.at(id);
        if (instance.location == destination)
            return InventoryError::None;
        if (instance.revision == std::numeric_limits<uint64_t>::max())
            return InventoryError::RevisionExhausted;
        result.changes.push_back({id, instance.revision + 1, ItemChangeKind::Moved, instance.location,
                                  destination, instance.quantity});
        instance.location = destination;
        ++instance.revision;
        return InventoryError::None;
    };
    auto returnToPack = [&](EntityId id) {
        const auto &instance = *draft.item(id);
        auto space = draft.findSpace(containers.backpack, instance.definition);
        if (!space)
            return InventoryError::NoSpace;
        return relocate(id, ContainerLocation{containers.backpack, *space});
    };
    if (!command.slot) {
        ItemDestination target = command.destination.value_or(AutoPlace{containers.backpack});
        if (auto error = draft.checkDestinationAccess(target, access); error != InventoryError::None)
            return reject(error);
        ItemLocation destination;
        if (auto error = draft.resolve(definition, target, destination, source.id); error != InventoryError::None)
            return reject(error);
        if (auto error = relocate(source.id, destination); error != InventoryError::None)
            return reject(error);
    } else {
        auto slot = *command.slot;
        auto previous = equipped(containers, slot);
        if (auto error = relocate(source.id, ContainerLocation{containers.equipment, {int(slot), 0}});
            error != InventoryError::None)
            return reject(error);
        if (previous && previous != source.id)
            if (auto error = returnToPack(previous); error != InventoryError::None)
                return reject(error);
        if (slot == EquipmentSlot::RightHand || slot == EquipmentSlot::LeftHand) {
            auto opposite =
                slot == EquipmentSlot::RightHand ? EquipmentSlot::LeftHand : EquipmentSlot::RightHand;
            auto other = draft.equipped(containers, opposite);
            if (other && !compatibleHands(definition, *catalog_.find(draft.item(other)->definition), actor))
                if (auto error = returnToPack(other); error != InventoryError::None)
                    return reject(error);
        }
    }
    if (result.changes.empty())
        result.transferred = 0;
    try {
        deriveEquipmentStats(draft, containers, actor);
    } catch (const std::runtime_error &) {
        return reject(InventoryError::UnsupportedEquipment);
    }
    if (replacement)
        *replacement = std::move(draft.state_);
    return result;
}
InventoryError InventoryService::preview(const EquipItem &command, const PlayerContainers &containers,
                                         const InventoryAccess &access, const EquipmentActor &actor) const {
    return planEquipment(command, containers, access, actor, nullptr).error;
}
InventoryResult InventoryService::equip(const EquipItem &command, const PlayerContainers &containers,
                                        const InventoryAccess &access, const EquipmentActor &actor) {
    InventoryState next;
    auto result = planEquipment(command, containers, access, actor, &next);
    if (result)
        state_ = std::move(next);
    return result;
}
} // namespace d2x