#include "inventory_order.hpp"
#include "operations.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace d2x {
std::vector<EntityId> inventoryItemOrder(const InventoryState &state) {
    std::vector<EntityId> result;
    std::set<EntityId> seen;
    for(const auto id:state.itemOrder) {
        if(!seen.insert(id).second) throw std::invalid_argument("Duplicate native inventory node");
        if(state.items.contains(id)) result.push_back(id);
    }
    for(const auto &[id,item]:state.items) if(!seen.contains(id)) {result.push_back(id);seen.insert(id);}
    return result;
}
void reinsertInventoryItem(InventoryState &state,EntityId id) {
    state.itemOrder=inventoryItemOrder(state);
    std::erase(state.itemOrder,id);
    if(state.items.contains(id)) state.itemOrder.push_back(id);
}
void prepareInventoryItemOrder(const InventoryState &previous,InventoryState &next,std::span<const ItemChange> changes) {
    next.itemOrder=inventoryItemOrder(previous);
    for(const auto &change:changes) {
        if(change.kind!=ItemChangeKind::Moved && change.kind!=ItemChangeKind::Created && change.kind!=ItemChangeKind::Removed) continue;
        std::erase(next.itemOrder,change.item);
        if(change.kind!=ItemChangeKind::Removed && next.items.contains(change.item)) next.itemOrder.push_back(change.item);
    }
    next.itemOrder=inventoryItemOrder(next);
}
}
