#pragma once
#include "core/math.hpp"
#include "core/id.hpp"
#include "world/collision.hpp"
#include "gameplay/skills/elemental_spec.hpp"
#include "gameplay/skills/amazon_missile.hpp"
#include <array>
#include <deque>
#include <vector>
#include <optional>

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
    bool childServerSent{}, returnFire{};
    bool canSlow{}, groundThrow{};
    int guidedRadius{};
    std::array<int,2> poisonVelocity{};
    int loopFrames{}, immolationRadius{};
    std::optional<BlizzardSpec> blizzard;
    std::optional<ArcSpec> chain;
    int chainCountDivisor{1};
    std::optional<MissileTargetBurst> targetBurst;
    bool blessedHammer{},holyBolt{};
};
struct ClientMissileVisual {
    ClientMissileVisual() = default;
    ClientMissileVisual(int missileId, Vec pos, Vec velocity, float age, float duration, Vec direction)
        : missileId(missileId), pos(pos), velocity(velocity), age(age), duration(duration), direction(direction) {}
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
    int remainingHits{};
    int slowPercent{};
    EntityId guidance{};bool guidanceSearched{};
    float animationOffset{};
    // Pair reconstruction and 73 once without merging independent creations.
    uint8_t creationSources{};
    Vec creationPosition, creationDirection;
    EntityId soundEmitter;
    uint64_t random = 0; // Presentation stream; the server's unit seed is not transmitted.
    std::deque<Vec> path{};
    std::vector<EntityId> contacts{};
};
enum class ClientMissileSource : uint8_t { Program = 0, Cast = 1, Synchronization = 2 };
struct ClientMissileTarget {
    EntityId id;
    Vec position;
    int size{};
    bool hostile{};
    int retaliation{-1},retaliationRank{1};
    bool undead{};
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
