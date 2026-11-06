#pragma once
#include "server/runtime/contracts.hpp"
#include "gameplay/character/persistent_character.hpp"
#include "gameplay/items/operations.hpp"
#include <vector>

namespace d2x::server {
struct ItemFact { TransactionId transaction; ItemChange change; };
// Sparse immutable projection captured at commit. Encoding never consults a
// later live inventory: several commands can commit before output is drained.
struct InventoryFact {
    PersistentCharacter projection;
    std::vector<ItemChange> changes;
    bool switchedWeapons{};
    // Original values for removal receipts; these are not live inventory items.
    std::vector<ItemInstance> removedItems{};
};
struct ItemTargetingFact { EntityId source; int cursor{-1}; int skill{-1}; };
struct ItemSpellEndFact {EntityId source;bool afterInventory{};int skill{-1};int quantity{};};
// Captured at the drop transaction; later visibility snapshots are not drops.
struct GroundDropFact { ItemInstance item; bool flip=true; };
struct GroundRemoveFact { EntityId item; };
struct GroundRestoredFact {EntityId item;uint64_t revision{};};
struct TradeItemsFact { PersistentCharacter projection; std::vector<ItemInstance> removed; bool completed{}; };
// Frozen rollback inventory; observers rebuild only the public equipment.
struct InventoryResetFact { PersistentCharacter projection; };
}
