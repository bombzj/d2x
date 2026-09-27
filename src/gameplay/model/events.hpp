#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/items/operations.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include "gameplay/quest/state.hpp"
#include <variant>

namespace d2x {
struct SkillCast {
    EntityId actor;
    int skillId = -1;
    Vec position;
};
struct WeaponAttackStarted {
    EntityId actor, target;
    bool projectile = false;
};
struct MissileImpact { int missileId; Vec position; };
struct MissileReleased { int missileId; };
struct SkillActivated { int skillId = -1; };
// Emitted once at the alive -> dead transition, including every fact a loot system needs.
struct EnemyDied {
    EntityId victim, killer;
    MonsterKind kind;
    RegionId region;
    Vec position;
    MonsterIdentity identity;
    int difficulty = 0;
    bool hirelingKill = false;
    uint64_t lootRandom = 0;
};
struct PlayerDied {
    EntityId player;
};
struct RegionEntered {
    RegionId region;
    std::optional<Vec> coordinateOffset;
};
struct ObjectInteracted {
    EntityId object;
    Interaction interaction;
    std::string name;
    bool firstIntroduction = false;
    bool unlockedChest = false;
};
struct NpcDialogueStarted {
    EntityId object;
    std::string speaker, text;
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
struct VendorItemSold { EntityId vendor, item; unsigned price = 0; };
struct GambleStockOpened { EntityId npc; };
struct HirelingListOpened { EntityId npc; };
struct HirelingHired { EntityId npc; };
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
    bool needsKey = false;
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
struct QuestAdvanced {
    ActOneQuest quest;
    uint32_t stage;
};
using GameEvent = std::variant<SkillCast, SkillActivated, MissileImpact, MissileReleased, WeaponAttackStarted, EnemyDied, EnemyAttacked, EnemySkill2, EnemyHit, PlayerDied, RegionEntered, ObjectInteracted, NpcDialogueStarted, ItemsIdentified, VendorItemBought, VendorItemSold,
                               ItemChange, InventoryRejected, InventoryApplied, ItemPickedUp, PickupFailed,
                               ItemUsed, BeltEquipped, StorageOpened, StorageClosed, InteractionFailed,
                               LootDeferred, WaypointActivated, QuestAdvanced, GambleStockOpened,
                               HirelingListOpened, HirelingHired>;
} // namespace d2x
