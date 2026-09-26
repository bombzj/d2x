#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/model/state.hpp"

namespace d2x {
struct CharacterSaveData {
    uint64_t nextEntityId = 1;
    uint32_t mapSeed = 210;
    int difficulty = 0;
    RegionId lastRegion = RegionId::Encampment;
    PlayerState player;
    std::map<RegionId, float> waypoints;
    InventoryState inventory;
    PlayerContainers containers;
};
} // namespace d2x