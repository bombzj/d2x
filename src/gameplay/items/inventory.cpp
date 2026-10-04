#include "inventory.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace d2x {
const ItemInstance *InventoryService::item(EntityId id) const {
    auto found = state_.items.find(id);
    return found == state_.items.end() ? nullptr : &found->second;
}
const ContainerState *InventoryService::container(EntityId id) const {
    auto found = state_.containers.find(id);
    return found == state_.containers.end() ? nullptr : &found->second;
}
EntityId InventoryService::itemAt(EntityId id, Cell cell) const {
    auto c = container(id);
    if (!c || cell.x < 0 || cell.y < 0 || cell.x >= c->spec.columns || cell.y >= c->spec.rows)
        return {};
    for (const auto &[key, instance] : state_.items) {
        auto location = std::get_if<ContainerLocation>(&instance.location);
        if (!location || location->container != id)
            continue;
        if (c->spec.kind == ContainerKind::Equipment || c->spec.kind == ContainerKind::Corpse) {
            if (location->cell == cell)
                return key;
            continue;
        }
        const auto &def = *catalog_.find(instance.definition);
        if (cell.x >= location->cell.x && cell.x < location->cell.x + def.width &&
            cell.y >= location->cell.y && cell.y < location->cell.y + def.height)
            return key;
    }
    return {};
}
std::vector<EntityId> InventoryService::contents(EntityId id) const {
    std::vector<EntityId> result;
    for (const auto &[key, instance] : state_.items)
        if (auto location = std::get_if<ContainerLocation>(&instance.location);
            location && location->container == id)
            result.push_back(key);
    return result;
}
std::vector<EntityId> InventoryService::groundItems(RegionId region) const {
    std::vector<EntityId> result;
    for (const auto &[key, instance] : state_.items)
        if (auto location = std::get_if<GroundLocation>(&instance.location);
            location && location->region == region)
            result.push_back(key);
    return result;
}
EntityId InventoryService::createContainer(ContainerSpec specification) {
    if (!specification.owner || specification.columns < 1 || specification.rows < 1 ||
        specification.columns > 64 || specification.rows > 64)
        throw std::invalid_argument("Invalid inventory container");
    if (specification.kind == ContainerKind::Belt && (specification.columns != 4 || specification.rows > 4))
        throw std::invalid_argument("Belt must have four columns and one to four rows");
    switch (specification.kind) {
    case ContainerKind::Corpse:
        if (specification.columns != int(EquipmentSlot::Count) + 1 || specification.rows != 1)
            throw std::invalid_argument("Invalid corpse container dimensions");
        break;
    case ContainerKind::Equipment:
        if (specification.columns != int(EquipmentSlot::Count) || specification.rows != 1)
            throw std::invalid_argument("Invalid equipment container dimensions");
        break;
    case ContainerKind::Cursor:
        if (specification.columns != 1 || specification.rows != 1)
            throw std::invalid_argument("Cursor holds one item");
        break;
    case ContainerKind::BeltEquipment:
    case ContainerKind::Backpack:
    case ContainerKind::Belt:
    case ContainerKind::Stash:
    case ContainerKind::Chest:
    case ContainerKind::Cube:
        break;
    default:
        throw std::invalid_argument("Unknown inventory container kind");
    }
    auto id = ids_.allocate();
    state_.containers.emplace(id, ContainerState{id, specification});
    return id;
}
PlayerContainers InventoryService::createPlayerContainers(EntityId player) {
    // The stash dimensions are adapted from the mounted MPQ inventory.txt by content.
    PlayerContainers result;
    result.cursor = createContainer({player, ContainerKind::Cursor, 1, 1});
    result.backpack = createContainer({player, ContainerKind::Backpack, 10, 4});
    result.belt = createContainer({player, ContainerKind::Belt, 4, 1});
    result.stash = createContainer({player, ContainerKind::Stash, stashDimensions_.x, stashDimensions_.y});
    result.beltEquipment = createContainer({player, ContainerKind::BeltEquipment, 2, 1});
    result.equipment = createContainer({player, ContainerKind::Equipment, int(EquipmentSlot::Count), 1});
    result.hirelingEquipment = createContainer({player, ContainerKind::Equipment, int(EquipmentSlot::Count), 1});
    if (cubeDimensions_.x > 0 && cubeDimensions_.y > 0)
        result.cube = createContainer({player, ContainerKind::Cube, cubeDimensions_.x, cubeDimensions_.y});
    return result;
}
bool InventoryService::overlaps(const ItemDefinition &a, const ItemLocation &aPosition,
                                const ItemDefinition &b, const ItemLocation &bPosition) const {
    if (const auto *left = std::get_if<GroundLocation>(&aPosition)) {
        const auto *right = std::get_if<GroundLocation>(&bPosition);
        return right && left->region == right->region &&
            std::floor(left->position.x) == std::floor(right->position.x) &&
            std::floor(left->position.y) == std::floor(right->position.y);
    }
    auto left = std::get_if<ContainerLocation>(&aPosition);
    auto right = std::get_if<ContainerLocation>(&bPosition);
    return left && right && left->container == right->container && left->cell.x < right->cell.x + b.width &&
           left->cell.x + a.width > right->cell.x && left->cell.y < right->cell.y + b.height &&
           left->cell.y + a.height > right->cell.y;
}
InventoryError InventoryService::checkHandle(ItemHandle handle) const {
    auto source = item(handle.id);
    if (!source)
        return InventoryError::UnknownItem;
    if (source->revision != handle.revision)
        return InventoryError::SourceChanged;
    if (source->revision == std::numeric_limits<uint64_t>::max())
        return InventoryError::RevisionExhausted;
    return InventoryError::None;
}
InventoryError InventoryService::checkAccess(const ItemLocation &location,
                                             const InventoryAccess &access) const {
    if (!access.actor || !access.alive)
        return InventoryError::AccessDenied;
    if (std::holds_alternative<SocketLocation>(location))
        return InventoryError::RestrictedItem;
    if (auto ground = std::get_if<GroundLocation>(&location)) {
        if (!std::isfinite(ground->position.x) || !std::isfinite(ground->position.y) ||
            !std::isfinite(access.position.x) || !std::isfinite(access.position.y) ||
            !std::isfinite(access.reach) || access.reach < 0 || ground->region != access.region ||
            (ground->position - access.position).length() > access.reach)
            return InventoryError::AccessDenied;
    } else {
        auto c = container(std::get<ContainerLocation>(location).container);
        if (!c)
            return InventoryError::UnknownContainer;
        if (c->spec.kind == ContainerKind::BeltEquipment || c->spec.kind == ContainerKind::Equipment ||
            c->spec.kind == ContainerKind::Corpse)
            return InventoryError::RestrictedItem;
        if (c->spec.kind == ContainerKind::Chest)
            return access.openContainer == c->id ? InventoryError::None : InventoryError::AccessDenied;
        if (c->spec.kind == ContainerKind::Cube)
            return access.portableContainer == c->id ? InventoryError::None : InventoryError::AccessDenied;
        if (c->spec.owner != access.actor)
            return InventoryError::AccessDenied;
        if (c->spec.kind == ContainerKind::Stash && access.openContainer != c->id)
            return InventoryError::AccessDenied;
    }
    return InventoryError::None;
}
InventoryError InventoryService::checkDestinationAccess(const ItemDestination &destination,
                                                        const InventoryAccess &access) const {
    return std::visit(
        [&](const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, AutoPlace>)
                return checkAccess(ContainerLocation{value.container, {}}, access);
            else
                return checkAccess(value, access);
        },
        destination);
}
InventoryError InventoryService::checkCarryLimit(const ItemInstance &source,
                                                 const ItemLocation &destination,
                                                 const InventoryState &state, EntityId ignore) const {
    const auto *definition = catalog_.find(source.definition);
    const bool uniqueLimit = source.quality == ItemQuality::Unique &&
                             singleCarryUniques_.contains(source.specialRow);
    const bool questLimit = definition && definition->questTag != 0;
    if (!uniqueLimit && !questLimit)
        return InventoryError::None;
    auto ownerAt = [&](const ItemLocation &location, bool includeCorpse) -> EntityId {
        const auto *position = std::get_if<ContainerLocation>(&location);
        if (!position) return {};
        const auto found = state.containers.find(position->container);
        // Native stash is page 4 and participates in carry checks. Quest pickup
        // additionally examines corpses; non-quest carry1 does not.
        if (found == state.containers.end() || found->second.spec.kind == ContainerKind::Chest ||
            (!includeCorpse && found->second.spec.kind == ContainerKind::Corpse))
            return {};
        return found->second.spec.owner;
    };
    const auto owner = ownerAt(destination, false);
    if (!owner) return InventoryError::None;
    for (const auto &[id, other] : state.items) {
        if (id == source.id || id == ignore) continue;
        if (uniqueLimit && other.quality == ItemQuality::Unique &&
            other.specialRow == source.specialRow && ownerAt(other.location, false) == owner)
            return InventoryError::RestrictedItem;
        if (questLimit && ownerAt(other.location, true) == owner) {
            const auto *otherDefinition = catalog_.find(other.definition);
            if (otherDefinition && otherDefinition->questTag == definition->questTag &&
                (other.definition == source.definition ||
                 std::find(definition->questCarryConflicts.begin(), definition->questCarryConflicts.end(),
                           other.definition) != definition->questCarryConflicts.end()))
                return InventoryError::RestrictedItem;
        }
    }
    return InventoryError::None;
}
InventoryError InventoryService::checkPlacement(const ItemDefinition &definition,
                                                const ItemLocation &location, EntityId ignore,
                                                EntityId alsoIgnore) const {
    if (auto ground = std::get_if<GroundLocation>(&location)) {
        if (!std::isfinite(ground->position.x) || !std::isfinite(ground->position.y) ||
            ground->position.x < 0 || ground->position.y < 0)
            return InventoryError::InvalidLocation;
        if (groundPlacement_ && !groundPlacement_(*ground, *ground))
            return InventoryError::InvalidLocation;
        for (const auto &[id, other] : state_.items) {
            if (id == ignore || id == alsoIgnore) continue;
            const auto *placed = std::get_if<GroundLocation>(&other.location);
            if (placed && placed->region == ground->region &&
                std::floor(placed->position.x) == std::floor(ground->position.x) &&
                std::floor(placed->position.y) == std::floor(ground->position.y))
                return InventoryError::Occupied;
        }
        return InventoryError::None;
    }
    const auto &position = std::get<ContainerLocation>(location);
    if (definition.equipment.isType("gold"))
        return InventoryError::RestrictedItem;
    auto c = container(position.container);
    if (!c)
        return InventoryError::UnknownContainer;
    if (c->spec.kind == ContainerKind::BeltEquipment || c->spec.kind == ContainerKind::Equipment ||
        c->spec.kind == ContainerKind::Corpse)
        return InventoryError::RestrictedItem;
    if (c->spec.kind == ContainerKind::Cube && definition.opensCube)
        return InventoryError::RestrictedItem;
    if (c->spec.kind == ContainerKind::Belt &&
        (!definition.beltAllowed || definition.width != 1 || definition.height != 1))
        return InventoryError::RestrictedItem;
    if (c->spec.kind == ContainerKind::Cursor) {
        if (position.cell != Cell{}) return InventoryError::OutOfBounds;
    } else if (position.cell.x < 0 || position.cell.y < 0 || definition.width > c->spec.columns ||
        definition.height > c->spec.rows || position.cell.x > c->spec.columns - definition.width ||
        position.cell.y > c->spec.rows - definition.height)
        return InventoryError::OutOfBounds;
    for (const auto &[id, other] : state_.items) {
        if (id == ignore || id == alsoIgnore)
            continue;
        if (overlaps(definition, location, *catalog_.find(other.definition), other.location))
            return InventoryError::Occupied;
    }
    return InventoryError::None;
}
std::optional<Cell> InventoryService::findSpace(EntityId id, std::string_view code, EntityId ignore) const {
    auto c = container(id);
    auto def = catalog_.find(code);
    if (!c || !def)
        return std::nullopt;
    if (c->spec.kind == ContainerKind::Cursor)
        return checkPlacement(*def, ContainerLocation{id, {}}, ignore) == InventoryError::None
            ? std::optional<Cell>{Cell{}} : std::nullopt;
    for (int y = 0; y <= c->spec.rows - def->height; ++y)
        for (int x = 0; x <= c->spec.columns - def->width; ++x)
            if (checkPlacement(*def, ContainerLocation{id, {x, y}}, ignore) == InventoryError::None)
                return Cell{x, y};
    return std::nullopt;
}
InventoryError InventoryService::resolve(const ItemDefinition &def, const ItemDestination &destination,
                                         ItemLocation &location, EntityId ignore,
                                         std::optional<Vec> groundOrigin) const {
    return std::visit(
        [&](const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, AutoPlace>) {
                auto c = container(value.container);
                if (!c)
                    return InventoryError::UnknownContainer;
                if (c->spec.kind == ContainerKind::Belt &&
                    (!def.beltAllowed || def.width != 1 || def.height != 1))
                    return InventoryError::RestrictedItem;
                auto slot = findSpace(value.container, def.code, ignore);
                if (!slot)
                    return InventoryError::NoSpace;
                location = ContainerLocation{value.container, *slot};
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, GroundLocation>) {
                if (!std::isfinite(value.position.x) || !std::isfinite(value.position.y) ||
                    value.position.x < 0 || value.position.y < 0)
                    return InventoryError::InvalidLocation;
                const Vec origin{std::floor(value.position.x) + .5f, std::floor(value.position.y) + .5f};
                const GroundLocation field{value.region, groundOrigin.value_or(value.position)};
                // D2MOO COLLISION_GetFreeCoordinatesWithField: size 1, item occupancy,
                // expanding search below 50 subtiles, with a clear field from the drop origin.
                for (int radius = 0; radius < 50; ++radius) {
                    std::optional<GroundLocation> best;
                    int distance = 2 * radius + 1;
                    for (int y = -radius; y <= radius; ++y)
                        for (int x = -radius; x <= radius; ++x) {
                            if (std::max(std::abs(x), std::abs(y)) != radius ||
                                std::abs(x) + std::abs(y) >= distance) continue;
                            GroundLocation candidate{value.region, origin + Vec{float(x), float(y)}};
                            if (checkPlacement(def, candidate, ignore) != InventoryError::None ||
                                (groundPlacement_ && !groundPlacement_(field, candidate))) continue;
                            best = candidate;
                            distance = std::abs(x) + std::abs(y);
                        }
                    if (best) {
                        location = *best;
                        return InventoryError::None;
                    }
                }
                return InventoryError::NoSpace;
            } else
                location = value;
            return checkPlacement(def, location, ignore);
        },
        destination);
}
const char *inventoryErrorText(InventoryError error) {
    switch (error) {
    case InventoryError::RequirementsNotMet:
        return "Equipment requirements are not met.";
    case InventoryError::WrongClass:
        return "This equipment is for another class.";
    case InventoryError::UnsupportedEquipment:
        return "This equipment's rules are not implemented yet.";
    case InventoryError::Unidentified:
        return "Identify this item before equipping it.";
    case InventoryError::UnsupportedUse:
        return "This item cannot be used yet.";
    case InventoryError::None:
        return "";
    case InventoryError::UnknownDefinition:
        return "Unknown item type.";
    case InventoryError::UnknownItem:
        return "That item is no longer available.";
    case InventoryError::UnknownContainer:
        return "That container is not available.";
    case InventoryError::InvalidQuantity:
        return "Invalid item quantity.";
    case InventoryError::InvalidLocation:
        return "Cannot place an item there.";
    case InventoryError::OutOfBounds:
        return "That item does not fit there.";
    case InventoryError::Occupied:
        return "That space is occupied.";
    case InventoryError::NoSpace:
        return "Not enough room.";
    case InventoryError::RestrictedItem:
        return "That item is not allowed in this container.";
    case InventoryError::AccessDenied:
        return "You cannot access that item or container here.";
    case InventoryError::SourceChanged:
        return "That item has changed. Select it again.";
    case InventoryError::NotStackable:
        return "That item cannot be stacked.";
    case InventoryError::IncompatibleStack:
        return "Those items cannot be combined.";
    case InventoryError::StackFull:
        return "That stack is full.";
    case InventoryError::InvalidRequest:
        return "Invalid item operation.";
    case InventoryError::RevisionExhausted:
        return "That item cannot be changed further.";
    }
    return "Item operation failed.";
}
} // namespace d2x
