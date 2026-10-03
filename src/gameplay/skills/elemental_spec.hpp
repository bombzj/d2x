#pragma once
#include "gameplay/skills/firewall_spec.hpp"
#include <array>

namespace d2x {
struct FreezingAreaSpec {
    int radius = 0, radiusPerLevel = 0, radiusOverride = 0;
    int freezeFrames = 0, freezeFramesPerLevel = 0, freezeOverride = 0;
    int synergySkill = -1, synergyPercent = 0;
    int ejectaId = -1;
};
struct FreezingAreaCastSpec {
    int radius = 0, freezeFrames = 0;
};
struct BlizzardSpec {
    int radius = 0, emissionPeriod = 0;
    int shardId = -1, shardFrames = 0;
    int fallDistance = 0, fallRate = 0;
    int impactId = -1, impactFrames = 0;
};
struct ArcSpec {
    int visualId = -1, subloops = 1;
    int range = 0, count = 1, countPerLevel = 0;
    int nextDelay = 0;
};
struct MeteorSpec {
    int radius = 0, radiusPerLevel = 0, fireStep = 1;
    int fireFrames = 0, fireFramesPerLevel = 0;
    FirewallSpec fire;
    std::array<int, 5> fireMinimumPerLevel{}, fireMaximumPerLevel{};
    int fireSynergySkill = -1, fireSynergyPercent = 0;
    int fallId = -1, tailId = -1, explodeId = -1;
    int fallStart = 0, fallSpeed = 0, explodeDensity = 1;
    int lightId = -1, mediumId = -1, smallId = -1;
    int mediumDensity = 1, smallDensity = 1;
};
struct HeavenSpec {
    int delayFrames = 0, radius = 0, limit = 0, limitPerLevel = 0;
    int boltId = -1, boltFrames = 0, boltVelocity = 0;
    int minimum = 0, maximum = 0, synergySkill = -1, synergyPercent = 0;
    std::array<int, 5> minimumPerLevel{}, maximumPerLevel{};
    int healingMinimum = 0, healingMinimumPerLevel = 0;
    int healingMaximum = 0, healingMaximumPerLevel = 0;
};
struct FrozenOrbSpec {
    struct Child {
        int missileId = -1, velocity = 0, velocityPerLevel = 0;
        int lifetimeFrames = 0, rangePerLevel = 0;
    };
    Child bolt, nova;
    int emissionPeriod = 1, directionStep = 0, burstStep = 1;
    int novaTurnFrames = 0, novaTurnPeriod = 1;
};
struct FrozenOrbCastSpec {
    struct Child {
        int missileId = -1, lifetimeFrames = 0;
        float speed = 0;
    };
    Child bolt, nova;
    int lifetimeFrames = 0, emissionPeriod = 1, directionStep = 0, burstStep = 1;
    int novaTurnFrames = 0, novaTurnPeriod = 1;
};
} // namespace d2x
