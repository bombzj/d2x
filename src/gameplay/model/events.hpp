#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include <variant>

namespace d2x {
struct SkillCast {
    EntityId actor;
    Skill skill;
    Vec position;
};
struct MeleeAttack {
    EntityId actor, target;
};
// Emitted once at the alive -> dead transition, including every fact a loot system needs.
struct EnemyDied {
    EntityId victim, killer;
    MonsterKind kind;
    RegionId region;
    Vec position;
    MonsterIdentity identity;
    int difficulty = 0;
};
struct PlayerDied {
    EntityId player;
};
struct RegionEntered {
    RegionId region;
};
struct ObjectInteracted {
    EntityId object;
    Interaction interaction;
    std::string name;
};
struct InventoryRejected {
    EntityId item;
    InventoryError error;
};
struct InventoryApplied {
    EntityId requested, item;
    unsigned transferred;
};
struct ItemPickedUp {
    EntityId item;
    std::string definition;
    unsigned quantity;
};
struct ItemUsed {
    EntityId item;
    std::string definition;
};
struct BeltEquipped {};
struct StorageOpened {
    EntityId object, container;
};
struct StorageClosed {
    EntityId container;
};
struct InteractionFailed {
    EntityId object;
    std::string reason;
};
struct PickupFailed {
    EntityId item;
    std::string reason;
};
struct LootDeferred {
    EntityId source;
    std::string reason;
};
using GameEvent = std::variant<SkillCast, MeleeAttack, EnemyDied, PlayerDied, RegionEntered, ObjectInteracted,
                               ItemChange, InventoryRejected, InventoryApplied, ItemPickedUp, PickupFailed,
                               ItemUsed, BeltEquipped, StorageOpened, StorageClosed, InteractionFailed,
                               LootDeferred>;
} // namespace d2x
