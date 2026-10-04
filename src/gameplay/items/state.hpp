#pragma once
#include "core/id.hpp"
#include "gameplay/items/handle.hpp"
#include "gameplay/items/generation.hpp"
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

struct ItemInstance {
    struct SavedStat {
        int id = 0, parameter = 0, value = 0;
    };
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
    bool nativeProperties = false;
    std::vector<SavedStat> savedStats;
    std::array<std::vector<SavedStat>, 5> savedSetStats;
    uint32_t nativeSeed = 0, nativeFlags = 0, nativeGraphic = 0, nativeFormat = 101;
    bool nativeHasGraphic = false;
    unsigned nativeQuestDifficulty = 0;
    unsigned nativeMaxDurability = 0;
    unsigned sockets = 0;
    std::string personalizedName;
    ItemLocation location;
    ItemHandle handle() const { return {id, revision}; }
};
enum class ContainerKind { Backpack, Belt, Stash, Chest, BeltEquipment, Equipment, Cube, Cursor, Corpse };
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
    EntityId backpack, belt, stash, beltEquipment, equipment, cube, hirelingEquipment, cursor;
};
// Location is authoritative. Occupancy is derived, never a second mutable copy.
struct InventoryState {
    uint64_t creationRandom = 0; // Initialized by the owning session, not serialized in D2S.
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
