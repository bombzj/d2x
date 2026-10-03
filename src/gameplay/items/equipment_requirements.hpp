#pragma once
#include "gameplay/items/errors.hpp"
#include <functional>

namespace d2x {
struct ItemInstance;
struct ItemDefinition;
struct EquipmentActor;
// The resolver is synchronous and lazy: unidentified/unsupported/wrong-class
// items retain their original rejection before any property evaluation.
InventoryError checkEquipmentRequirements(const ItemInstance &item, const ItemDefinition &definition,
    const EquipmentActor &actor, const std::function<int()> &requirementPercent);
} // namespace d2x
