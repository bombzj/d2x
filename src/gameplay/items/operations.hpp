#pragma once
#include "equipment_rules.hpp"
#include "state.hpp"
#include "errors.hpp"
#include <optional>

namespace d2x {
enum class ItemChangeKind { Created, Moved, QuantityChanged, Removed, DurabilityChanged };
struct ItemChange {
    EntityId item;
    uint64_t revision;
    ItemChangeKind kind;
    std::optional<ItemLocation> before, after;
    unsigned quantity;
};
struct InventoryResult {
    InventoryError error = InventoryError::None;
    EntityId item;
    unsigned transferred = 0;
    std::vector<ItemChange> changes;
    explicit operator bool() const { return error == InventoryError::None; }
};
struct MoveItem {
    ItemHandle item;
    ItemDestination destination;
};
struct TransferItem {
    ItemHandle item;
    EntityId destination;
};
struct EquipBelt {
    ItemHandle item; // Backpack -> equipped, or equipped -> backpack.
    std::optional<ItemDestination> destination = std::nullopt;
};
struct EquipItem {
    ItemHandle item;
    std::optional<EquipmentSlot> slot;
    std::optional<ItemDestination> destination = std::nullopt;
};
struct UseItem {
    ItemHandle item;
};
struct LoadBook {
    ItemHandle scroll, book;
};
struct IdentifyItem {
    ItemHandle source, target;
};
struct UseBeltColumn {
    int column;
    bool hireling = false;
};
struct SwapItems {
    ItemHandle first, second;
};
struct SplitStack {
    ItemHandle source;
    unsigned quantity;
    ItemDestination destination;
};
struct MergeStacks {
    ItemHandle source, target;
    unsigned quantity = 0; // Zero transfers as much as fits, without relocating either stack.
};
} // namespace d2x
