#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include "world/collision.hpp"
#include <array>
#include <deque>
#include <vector>

namespace d2x {
// MPQ client programs only. No damage, chance-to-hit or status application.
struct ClientMissileProgram {
    int function{}, hitFunction{}, velocity{}, velocityPerLevel{}, acceleration{}, maximumVelocity{};
    int frames{}, framesPerLevel{}, explosion{-1};
    MissileCollisionRule collision;
    bool collide{}, killOnContact{}, explodeOnExpiry{};
    std::array<int, 5> parameters{};
    std::array<int, 3> hitParameters{}, children{-1,-1,-1};
    std::array<int, 4> hitChildren{-1,-1,-1,-1};
    bool childServerSent{};
};
struct ClientMissileVisual {
    int missileId = -1;
    Vec pos, velocity;
    float age = 0, duration = 0;
    Vec direction;
    // Existing static effects leave flight false. Flights advance at native 25 Hz.
    bool flight{};
    int level{1}, frame{}, velocityFixed{}, acceleration{}, directionIndex{};
    Vec turnTarget;
    EntityId owner;
    bool hostile{};
    int pierce{};
    float animationOffset{};
    EntityId soundEmitter;
    std::deque<Vec> path{};
    std::vector<EntityId> contacts{};
};
struct ClientMissileTarget {
    EntityId id;
    Vec position;
    int size{};
    bool hostile{};
};
// Prepared from imported skill data by SceneAssets; no damage or cast rules.
struct BlizzardVisual {
    int fallDistance = 0, fallRate = 0;
    int impactId = -1, impactFrames = 0;
};
struct MeteorVisual {
    int fireFrames = 0;
    int explodeId = -1, explodeDensity = 1;
    int lightId = -1, mediumId = -1, smallId = -1;
    int mediumDensity = 1, smallDensity = 1;
};
} // namespace d2x
