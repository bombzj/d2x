#pragma once
#include "gameplay/model/definitions.hpp"
#include "gameplay/monsters/state.hpp"
#include "gameplay/monsters/monster_spawn.hpp"
#include "gameplay/skills/missile.hpp"
#include <map>
#include <vector>

namespace d2x {
// Area combat state survives travel. Ground item ownership lives in session InventoryState.
struct AreaState {
    RegionId region = RegionId::Encampment;
    std::vector<Enemy> enemies;
    std::vector<MonsterSpawn> pendingSpawns;
    std::vector<Missile> missiles;
    std::vector<Effect> effects;
    std::map<EntityId, float> novaHitUntil;
    int kills = 0;
    bool initialized = false;
};
} // namespace d2x
