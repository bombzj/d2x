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
struct EnemyAttacked {
    EntityId attacker;
    MonsterKind kind;
    int mode = 1;
};
struct EnemySkill2 {
    EntityId caster;
};
struct EnemyHit {
    EntityId victim;
    MonsterKind kind;
};
struct ItemsIdentified {
    EntityId npc;
    unsigned count = 0, goldSpent = 0;
};
struct VendorItemBought {
    EntityId vendor, item;
    uint32_t slot = 0;
    unsigned price = 0;
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
struct WaypointActivated {
    EntityId object;
};
using GameEvent = std::variant<SkillCast, MeleeAttack, EnemyDied, EnemyAttacked, EnemySkill2, EnemyHit, PlayerDied, RegionEntered, ObjectInteracted, ItemsIdentified, VendorItemBought,
                               ItemChange, InventoryRejected, InventoryApplied, ItemPickedUp, PickupFailed,
                               ItemUsed, BeltEquipped, StorageOpened, StorageClosed, InteractionFailed,
                               LootDeferred, WaypointActivated>;
} // namespace d2x
