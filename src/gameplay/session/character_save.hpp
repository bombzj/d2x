#pragma once
#include "gameplay/items/state.hpp"
#include "gameplay/character/record.hpp"
#include "gameplay/player/corpse.hpp"

namespace d2x {
struct CharacterSaveData {
    uint64_t nextEntityId = 1;
    uint32_t mapSeed = 0;
    int difficulty = 0;
    RegionId lastRegion = RegionId::Encampment;
    CharacterRecord player;
    std::map<RegionId, float> waypoints;
    InventoryState inventory;
    PlayerContainers containers;
    std::vector<PlayerCorpse> corpses;
};
} // namespace d2x
