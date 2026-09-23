#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/loot/loot.hpp"
#include "gameplay/model/state.hpp"
#include <set>

namespace d2x {
struct NpcMotionState {
    EntityId id;
    Vec position;
    Vec look;
    std::deque<Vec> route;
    float wait = 0;
    int target = -1;
    uint64_t random = 0;
};
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
    std::vector<NpcMotionState> npcMotions;
    std::map<EntityId, std::set<uint32_t>> soldVendorOffers;
};
} // namespace d2x
