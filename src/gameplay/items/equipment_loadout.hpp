#pragma once
#include "gameplay/items/equipment_rules.hpp"
#include "core/id.hpp"
#include "gameplay/items/errors.hpp"
#include <array>
#include <functional>
#include <vector>

namespace d2x {
struct ItemInstance;
struct ItemDefinition;
struct EquipmentItem {
    const ItemInstance *instance = nullptr;
    const ItemDefinition *definition = nullptr;
    explicit operator bool() const { return instance && definition; }
};
// Borrowed only within one synchronous authority call. No ownership, cache,
// inventory mutation or actor/character lookup; rebuild after a transaction.
struct EquipmentLoadout {
    std::array<EquipmentItem, size_t(EquipmentSlot::Count)> equipped{};
    std::vector<EquipmentItem> backpack;
    std::function<int(const ItemInstance &)> requirementPercent;
    std::function<unsigned(const ItemInstance &)> maximumDurability;
    InventoryError requirements(EquipmentItem item, const EquipmentActor &actor) const;
    const ItemInstance *usable(EquipmentSlot slot, const EquipmentActor &actor) const;
    EquipmentItem find(EntityId item) const;
};
} // namespace d2x
