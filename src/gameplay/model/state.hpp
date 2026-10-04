#pragma once
#include "gameplay/combat/relations.hpp"
#include "gameplay/model/definitions.hpp"
#include "gameplay/player/state.hpp"
#include "gameplay/monsters/state.hpp"
#include "gameplay/areas/state.hpp"
#include "gameplay/monsters/population_settings.hpp"
#include "gameplay/skills/missile.hpp"
#include <map>
#include <string>
#include <vector>

namespace d2x {
// Player combat state survives travel. GameSession separately owns inventory/container state.
struct TownPortalState {
    bool active = false;
    uint64_t revision = 0;
    RegionId field = RegionId::Encampment;
    Vec fieldPosition, townPosition;
    float openedAt = 0;
    bool consumedOnReturn = true;
};
struct WorldState {
    uint32_t mapSeed = 0;
    PopulationSettings population;
    PlayerState player;
    std::vector<Enemy> companions; // Owner-bound monsters travel with their controller.
    CombatRelations relations;
    AreaState area;
    EffectFrame frame = 0;
    float time = 0;
    std::string message;
    TownPortalState portal;
    std::vector<TownPortalState> publicPortals;
    uint64_t nextPortalRevision = 0;
    // Negative activation time denotes a waypoint already open before this game.
    std::map<RegionId, float> waypoints;
};
} // namespace d2x
