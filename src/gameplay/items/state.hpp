#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include <map>
#include <cstdint>
#include <variant>
#include <vector>

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

enum class ItemQuality { Normal, Magic, Rare, Set, Unique, Superior, Inferior };
struct ItemAffixInstance {
    bool prefix = false;
    int32_t row = -1;
    std::vector<int32_t> propertyRolls;
};
struct ItemGeneration {
    ItemQuality quality = ItemQuality::Normal;
    int32_t specialRow = -1;
    int32_t requiredLevel = 0;
    int32_t gradeRow = -1;
    int32_t rarePrefixRow = -1, rareSuffixRow = -1;
    std::vector<int32_t> propertyRolls;
    std::vector<ItemAffixInstance> affixes;
};
struct ItemHandle {
    EntityId id;
    uint64_t revision = 0;
};
struct ItemInstance {
    EntityId id;
    std::string definition;
    unsigned quantity = 1, durability = 0, charges = 0;
    ItemQuality quality = ItemQuality::Normal;
    bool identified = true;
    unsigned level = 1;
    uint64_t revision = 1;
    int defense = 0;
    int32_t specialRow = -1, requiredLevel = 0;
    int32_t gradeRow = -1;
    int32_t rarePrefixRow = -1, rareSuffixRow = -1;
    int32_t grantedSkill = -1; // Original CharStats.StartSkill on the first starter item.
    std::vector<int32_t> propertyRolls;
    std::vector<ItemAffixInstance> affixes;
    ItemLocation location;
    ItemHandle handle() const { return {id, revision}; }
};
enum class ContainerKind { Backpack, Belt, Stash, Chest, BeltEquipment, Equipment, Cube };
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
    EntityId backpack, belt, stash, beltEquipment, equipment, cube, hirelingEquipment;
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
    EntityId portableContainer;
};
} // namespace d2x
