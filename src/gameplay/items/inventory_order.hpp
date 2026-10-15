#pragma once
#include "state.hpp"
#include <span>
namespace d2x {
struct ItemChange;
// Return live nodes in order. A draft may still contain removed node IDs;
// newly allocated nodes absent from the sequence are appended in GUID order.
std::vector<EntityId> inventoryItemOrder(const InventoryState &);
void reinsertInventoryItem(InventoryState &,EntityId);
void prepareInventoryItemOrder(const InventoryState &previous,InventoryState &next,
    std::span<const ItemChange> changes);
}
