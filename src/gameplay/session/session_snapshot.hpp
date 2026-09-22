#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/model/state.hpp"

namespace d2x {
// Value-only boundary between the live session and persistence. No UI, archive,
// grid pointers, pending commands, or storage access grants cross this boundary.
struct SessionSnapshot {
    uint64_t contentFingerprint = 0, nextEntityId = 1;
    std::vector<std::string> maps;
    WorldState world;
    std::vector<AreaState> inactiveAreas;
    InventoryState inventory;
    PlayerContainers containers;
    LootState loot;
};
} // namespace d2x
