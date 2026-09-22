#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include <map>
#include <variant>

namespace d2x {
struct Cell {
    int x = 0, y = 0;
    auto operator<=>(const Cell &) const = default;
};
struct GroundLocation {
    RegionId region = RegionId::Encampment;
    Vec position;
    bool operator==(const GroundLocation &other) const {
        return region == other.region && position.x == other.position.x && position.y == other.position.y;
    }
};
struct ContainerLocation {
    EntityId container;
    Cell cell;
    auto operator<=>(const ContainerLocation &) const = default;
};
using ItemLocation = std::variant<GroundLocation, ContainerLocation>;
struct AutoPlace {
    EntityId container;
};
using ItemDestination = std::variant<GroundLocation, ContainerLocation, AutoPlace>;

enum class ItemQuality { Normal, Magic, Rare, Set, Unique };
struct ItemHandle {
    EntityId id;
    uint64_t revision = 0;
};
struct ItemInstance {
    EntityId id;
    std::string definition;
    unsigned quantity = 1, durability = 0;
    ItemQuality quality = ItemQuality::Normal;
    unsigned level = 1;
    uint64_t revision = 1;
    int defense = 0;
    ItemLocation location;
    ItemHandle handle() const { return {id, revision}; }
};
enum class ContainerKind { Backpack, Belt, Stash, Chest, BeltEquipment, Equipment };
struct ContainerSpec {
    EntityId owner;
    ContainerKind kind = ContainerKind::Backpack;
    int columns = 10, rows = 4;
};
struct ContainerState {
    EntityId id;
    ContainerSpec spec;
};
struct PlayerContainers {
    EntityId backpack, belt, stash, beltEquipment, equipment;
};
// Location is authoritative. Occupancy is derived, never a second mutable copy.
struct InventoryState {
    uint64_t creationRandom = (uint64_t(666) << 32) | 210;
    std::map<EntityId, ItemInstance> items;
    std::map<EntityId, ContainerState> containers;
};
// Built by GameSession from world state; never accepted from UI commands.
struct InventoryAccess {
    EntityId actor;
    bool alive = true;
    RegionId region = RegionId::Encampment;
    Vec position;
    EntityId openContainer;
    float reach = 4;
};
} // namespace d2x
