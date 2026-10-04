#pragma once
#include "core/id.hpp"
#include "gameplay/model/definitions.hpp"
#include <cstdint>
#include <vector>

namespace d2x {
// Items live exclusively in InventoryState. A corpse is an owned world object,
// not a combatant, and its sealed container is inaccessible to ordinary UI moves.
struct PlayerCorpse {
    EntityId id, owner, items;
    RegionId region = RegionId::Encampment;
    Vec position, look;
    uint64_t recoverableExperience = 0; // Same-game recovery only; never persisted.
    uint32_t nativeUnknown = 0;
};
inline std::vector<EntityId> corpseContainers(const std::vector<PlayerCorpse> &corpses) {
    std::vector<EntityId> result;
    for (const auto &corpse : corpses) result.push_back(corpse.items);
    return result;
}
} // namespace d2x
