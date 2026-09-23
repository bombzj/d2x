#pragma once
#include "gameplay/items/state.hpp"
#include <vector>

namespace d2x {
// Original D2Game Cain service charges 100 gold per item until his rescue reward.
// Quest progress is not modeled yet, so this is the available price tier.
inline constexpr unsigned cainIdentifyCost = 100;
struct IdentificationPlan {
    std::vector<EntityId> items;
    unsigned cost = 0;
};
IdentificationPlan planCainIdentification(const InventoryState &inventory,
                                          const PlayerContainers &containers);
void applyCainIdentification(InventoryState &inventory, const IdentificationPlan &plan);
} // namespace d2x
